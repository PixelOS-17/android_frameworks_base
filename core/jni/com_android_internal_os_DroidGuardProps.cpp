/*
 * Copyright (C) 2026 The PixelOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "Zygote"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <log/log.h>

// DroidGuard compares its native property reads with the Java build fields.
// The staged values live in persist.sys.pif.*, and every other process has to
// keep seeing the real ro.* values. libc stays unmodified: this process already
// has the property files mapped, so copy the areas that need a new value and
// map the copies over the same addresses. The shared pages are never written.
//
// Layout below has to match bionic's prop_area, prop_info, and prop_trie_node.
// A mismatch makes specialization leave the shared mappings in place.

static constexpr char kProcessName[] = "com.google.android.gms.unstable";
static constexpr char kStagingPrefix[] = "persist.sys.pif.";
static constexpr char kCustomCount[] = "persist.sys.pif.x.count";
static constexpr char kCustomSlotPrefix[] = "persist.sys.pif.x.";
static constexpr int kCustomSlotMax = 32;

static constexpr uint32_t kAreaMagic = 0x504f5250;
static constexpr uint32_t kAreaVersion = 0xfc6ed0ab;
static constexpr uint32_t kLongFlag = 1u << 16;
static constexpr size_t kLongErrorSize = 56;
static constexpr char kLongError[] = "Must use __system_property_read_callback() to read";
static_assert(sizeof(kLongError) <= kLongErrorSize, "long property error no longer fits");

struct PropAreaLayout {
    uint32_t bytes_used;
    std::atomic<uint32_t> serial;
    uint32_t magic;
    uint32_t version;
    uint32_t reserved[28];
};
static_assert(sizeof(PropAreaLayout) == 128, "prop_area header drifted");

struct PropInfoLayout {
    std::atomic<uint32_t> serial;
    union {
        char value[PROP_VALUE_MAX];
        struct {
            char error_message[kLongErrorSize];
            uint32_t offset;
        } long_property;
    };
};
static_assert(sizeof(PropInfoLayout) == 96, "prop_info prefix drifted");
static_assert(offsetof(PropInfoLayout, long_property.offset) == 60, "long property offset drifted");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "property serial must be lock-free");
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(uint32_t), "property serial must be 4 bytes");

struct TrieNodeLayout {
    uint32_t namelen;
    std::atomic<uint32_t> prop;
    std::atomic<uint32_t> left;
    std::atomic<uint32_t> right;
    std::atomic<uint32_t> children;
};
static_assert(sizeof(TrieNodeLayout) == 20, "prop_trie_node drifted");

struct Staged {
    bool present = false;
    char value[PROP_VALUE_MAX] = {};
};

struct Rule {
    std::string match;
    std::string value;
};

struct Snapshot {
    Staged manufacturer;
    Staged brand;
    Staged model;
    Staged device;
    Staged name;
    Staged build_id;
    Staged security_patch;
    Staged api_level;
    Staged build_type;
    Staged build_tags;
    Staged description;
    Staged flavor;
    std::vector<Rule> exact;
    std::vector<Rule> suffix;
};

struct Patch {
    uintptr_t prop = 0;
    std::string name;
    std::string value;
};

struct Mapping {
    uintptr_t start = 0;
    uintptr_t end = 0;
};

struct Prepared {
    Mapping mapping;
    std::vector<Patch> patches;
    void* copy = nullptr;
};

static bool StartsWith(const char* value, const char* prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

static bool EndsWith(const char* value, size_t length, const char* suffix) {
    size_t suffix_length = strlen(suffix);
    return length >= suffix_length &&
            memcmp(value + length - suffix_length, suffix, suffix_length) == 0;
}

static void LoadStaged(const char* name, Staged* out) {
    int length = __system_property_get(name, out->value);
    out->present = length > 0;
}

static void LoadSnapshot(Snapshot* snap) {
    LoadStaged("persist.sys.pif.product.manufacturer", &snap->manufacturer);
    LoadStaged("persist.sys.pif.product.brand", &snap->brand);
    LoadStaged("persist.sys.pif.product.model", &snap->model);
    LoadStaged("persist.sys.pif.product.device", &snap->device);
    LoadStaged("persist.sys.pif.product.name", &snap->name);
    LoadStaged("persist.sys.pif.build.id", &snap->build_id);
    LoadStaged("persist.sys.pif.security_patch", &snap->security_patch);
    LoadStaged("persist.sys.pif.api_level", &snap->api_level);
    LoadStaged("persist.sys.pif.build.type", &snap->build_type);
    LoadStaged("persist.sys.pif.build.tags", &snap->build_tags);
    LoadStaged("persist.sys.pif.build.description", &snap->description);
    LoadStaged("persist.sys.pif.build.flavor", &snap->flavor);

    char raw[PROP_VALUE_MAX];
    if (__system_property_get(kCustomCount, raw) <= 0) {
        return;
    }
    int count = 0;
    for (const char* cursor = raw; *cursor >= '0' && *cursor <= '9'; ++cursor) {
        if (count > kCustomSlotMax / 10) {
            break;
        }
        count = count * 10 + (*cursor - '0');
    }
    if (count > kCustomSlotMax) {
        count = kCustomSlotMax;
    }
    for (int i = 0; i < count; ++i) {
        char slot[sizeof(kCustomSlotPrefix) + 2];
        memcpy(slot, kCustomSlotPrefix, sizeof(kCustomSlotPrefix) - 1);
        char* end = slot + sizeof(kCustomSlotPrefix) - 1;
        if (i >= 10) {
            *end++ = static_cast<char>('0' + i / 10);
        }
        *end++ = static_cast<char>('0' + i % 10);
        *end = '\0';

        char entry[PROP_VALUE_MAX];
        if (__system_property_get(slot, entry) <= 0) {
            continue;
        }
        char* eq = strchr(entry, '=');
        if (eq == nullptr || eq == entry) {
            continue;
        }
        *eq = '\0';
        Rule rule;
        rule.value = eq + 1;
        if (entry[0] == '*') {
            if (entry[1] == '\0') {
                continue;
            }
            rule.match = entry + 1;
            snap->suffix.push_back(std::move(rule));
        } else {
            rule.match = entry;
            snap->exact.push_back(std::move(rule));
        }
    }
}

static const Staged* ProductStaged(const Snapshot& snap, const char* name) {
    if (!StartsWith(name, "ro.product.")) {
        return nullptr;
    }
    const char* field = name + strlen("ro.product.");
    const char* dot = strchr(field, '.');
    if (dot != nullptr) {
        if (dot == field) {
            return nullptr;
        }
        field = dot + 1;
        if (strchr(field, '.') != nullptr) {
            return nullptr;
        }
    }
    const Staged* staged = nullptr;
    if (strcmp(field, "manufacturer") == 0) {
        staged = &snap.manufacturer;
    } else if (strcmp(field, "brand") == 0) {
        staged = &snap.brand;
    } else if (strcmp(field, "model") == 0) {
        staged = &snap.model;
    } else if (strcmp(field, "device") == 0) {
        staged = &snap.device;
    } else if (strcmp(field, "name") == 0) {
        staged = &snap.name;
    }
    if (staged == nullptr || !staged->present) {
        return nullptr;
    }
    return staged;
}

// Same order as the previous libc hook: an exact custom entry wins, then the
// fixed staging props, then the two hardcoded service values, then a suffix.
static bool Lookup(const Snapshot& snap, const char* name, std::string* out) {
    if (name == nullptr || StartsWith(name, kStagingPrefix)) {
        return false;
    }
    for (const Rule& rule : snap.exact) {
        if (rule.match == name) {
            *out = rule.value;
            return true;
        }
    }

    const Staged* fixed = nullptr;
    if (strcmp(name, "ro.build.product") == 0) {
        fixed = snap.device.present ? &snap.device : nullptr;
    } else if (strcmp(name, "ro.build.description") == 0) {
        fixed = snap.description.present ? &snap.description : nullptr;
    } else if (strcmp(name, "ro.build.flavor") == 0) {
        fixed = snap.flavor.present ? &snap.flavor : nullptr;
    } else if ((fixed = ProductStaged(snap, name)) != nullptr) {
        // The product helper already requires a staged value.
    } else if (StartsWith(name, "ro.")) {
        size_t length = strlen(name);
        const Staged* staged = nullptr;
        if (EndsWith(name, length, ".build.id")) {
            staged = &snap.build_id;
        } else if (EndsWith(name, length, ".security_patch") ||
                   EndsWith(name, length, ".real_security_patch")) {
            staged = &snap.security_patch;
        } else if (EndsWith(name, length, "api_level")) {
            staged = &snap.api_level;
        } else if (EndsWith(name, length, ".build.type")) {
            staged = &snap.build_type;
        } else if (EndsWith(name, length, ".build.tags")) {
            staged = &snap.build_tags;
        }
        if (staged != nullptr && staged->present) {
            fixed = staged;
        }
    }
    if (fixed != nullptr) {
        *out = fixed->value;
        return true;
    }
    if (strcmp(name, "init.svc.adbd") == 0) {
        *out = "stopped";
        return true;
    }
    if (strcmp(name, "sys.usb.state") == 0) {
        *out = "mtp";
        return true;
    }
    size_t length = strlen(name);
    for (const Rule& rule : snap.suffix) {
        if (EndsWith(name, length, rule.match.c_str())) {
            *out = rule.value;
            return true;
        }
    }
    return false;
}

static bool SingleThreaded() {
    DIR* dir = opendir("/proc/self/task");
    if (dir == nullptr) {
        return false;
    }
    int threads = 0;
    while (dirent* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        ++threads;
    }
    closedir(dir);
    return threads == 1;
}

static bool ReadMaps(std::vector<Mapping>* maps) {
    FILE* fp = fopen("/proc/self/maps", "re");
    if (fp == nullptr) {
        return false;
    }
    char line[4096];
    while (fgets(line, sizeof(line), fp) != nullptr) {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[5] = {};
        char path[1024] = {};
        if (sscanf(line, "%lx-%lx %4s %*s %*s %*s %1023s", &start, &end, perms, path) < 4) {
            continue;
        }
        if (perms[0] != 'r' || strncmp(path, "/dev/__properties__/", 20) != 0 || end <= start) {
            continue;
        }
        maps->push_back(Mapping{static_cast<uintptr_t>(start), static_cast<uintptr_t>(end)});
    }
    fclose(fp);
    return true;
}

static void AddName(std::vector<std::string>* names, std::string name) {
    names->push_back(std::move(name));
}

// Opening a candidate maps the SELinux property area that holds it. The walk
// below then sees every sibling in that area, including names this list misses.
static void OpenCandidateAreas(const Snapshot& snap) {
    static constexpr const char* kParts[] = {
            "bootimage", "odm", "odm_dlkm", "product", "system", "system_dlkm",
            "system_ext", "vendor", "vendor_dlkm",
    };
    static constexpr const char* kFields[] = {
            "manufacturer", "brand", "model", "device", "name",
    };
    std::vector<std::string> names;
    for (const char* field : kFields) {
        AddName(&names, std::string("ro.product.") + field);
        for (const char* part : kParts) {
            AddName(&names, std::string("ro.product.") + part + "." + field);
        }
    }
    AddName(&names, "ro.build.id");
    AddName(&names, "ro.build.type");
    AddName(&names, "ro.build.tags");
    AddName(&names, "ro.build.product");
    AddName(&names, "ro.build.description");
    AddName(&names, "ro.build.flavor");
    AddName(&names, "ro.build.version.security_patch");
    AddName(&names, "ro.build.version.real_security_patch");
    AddName(&names, "ro.product.first_api_level");
    AddName(&names, "ro.board.first_api_level");
    AddName(&names, "ro.board.api_level");
    AddName(&names, "ro.vendor.api_level");
    for (const char* part : kParts) {
        std::string prefix = std::string("ro.") + part + ".build.";
        AddName(&names, prefix + "id");
        AddName(&names, prefix + "type");
        AddName(&names, prefix + "tags");
        AddName(&names, prefix + "version.security_patch");
        AddName(&names, prefix + "version.real_security_patch");
    }
    for (const Rule& rule : snap.exact) {
        names.push_back(rule.match);
    }
    // A suffix rule is applied while walking areas opened above. Also open the
    // area of the suffix with a ro. prefix so a wildcard such as ".fingerprint"
    // is visible when that area was not already mapped.
    for (const Rule& rule : snap.suffix) {
        if (rule.match.empty()) {
            continue;
        }
        if (rule.match[0] == '.') {
            names.push_back(std::string("ro") + rule.match);
            names.push_back(std::string("ro.build") + rule.match);
            names.push_back(std::string("ro.vendor.build") + rule.match);
            names.push_back(std::string("ro.product.build") + rule.match);
            names.push_back(std::string("ro.odm.build") + rule.match);
            names.push_back(std::string("ro.system.build") + rule.match);
            names.push_back(std::string("ro.system_ext.build") + rule.match);
        } else {
            names.push_back(std::string("ro.") + rule.match);
            names.push_back(std::string("ro.product.") + rule.match);
            names.push_back(std::string("ro.board.") + rule.match);
        }
    }
    names.emplace_back("init.svc.adbd");
    names.emplace_back("sys.usb.state");
    for (const std::string& name : names) {
        __system_property_find(name.c_str());
    }
}

static TrieNodeLayout* TrieAt(uint8_t* base, size_t size, uint32_t offset) {
    if (sizeof(PropAreaLayout) > size) {
        return nullptr;
    }
    size_t data_size = size - sizeof(PropAreaLayout);
    if (static_cast<size_t>(offset) + sizeof(TrieNodeLayout) > data_size) {
        return nullptr;
    }
    return reinterpret_cast<TrieNodeLayout*>(base + sizeof(PropAreaLayout) + offset);
}

static bool ReadCurrentValue(uint8_t* base, size_t size, uintptr_t prop, const PropInfoLayout* info,
                             std::string* out) {
    uint32_t serial = info->serial.load(std::memory_order_relaxed);
    if ((serial & kLongFlag) != 0) {
        uintptr_t live = prop + info->long_property.offset;
        uintptr_t start = reinterpret_cast<uintptr_t>(base);
        if (info->long_property.offset == 0 || live < start || live >= start + size) {
            return false;
        }
        size_t remain = start + size - live;
        const char* text = reinterpret_cast<const char*>(live);
        size_t length = strnlen(text, remain);
        if (length == remain) {
            return false;
        }
        out->assign(text, length);
        return true;
    }
    uint32_t length = serial >> 24;
    if (length >= PROP_VALUE_MAX) {
        return false;
    }
    out->assign(info->value, strnlen(info->value, length));
    return true;
}

static bool WalkArea(const Mapping& mapping, const Snapshot& snap, std::vector<Patch>* patches) {
    size_t size = mapping.end - mapping.start;
    auto* base = reinterpret_cast<uint8_t*>(mapping.start);
    auto* area = reinterpret_cast<PropAreaLayout*>(base);
    if (size < sizeof(PropAreaLayout) || area->magic != kAreaMagic ||
            area->version != kAreaVersion) {
        return true;
    }

    std::vector<uint32_t> stack;
    stack.push_back(0);
    size_t seen = 0;
    while (!stack.empty()) {
        if (++seen > 100000) {
            return false;
        }
        uint32_t offset = stack.back();
        stack.pop_back();
        TrieNodeLayout* node = TrieAt(base, size, offset);
        if (node == nullptr) {
            return false;
        }
        uint32_t left = node->left.load(std::memory_order_relaxed);
        uint32_t right = node->right.load(std::memory_order_relaxed);
        uint32_t children = node->children.load(std::memory_order_relaxed);
        uint32_t prop = node->prop.load(std::memory_order_relaxed);
        if (left != 0) stack.push_back(left);
        if (right != 0) stack.push_back(right);
        if (children != 0) stack.push_back(children);
        if (prop == 0) {
            continue;
        }
        if (sizeof(PropAreaLayout) + static_cast<size_t>(prop) + sizeof(PropInfoLayout) > size) {
            return false;
        }
        uintptr_t live = mapping.start + sizeof(PropAreaLayout) + prop;
        auto* info = reinterpret_cast<PropInfoLayout*>(base + sizeof(PropAreaLayout) + prop);
        size_t name_at = sizeof(PropAreaLayout) + prop + sizeof(PropInfoLayout);
        size_t remain = size - name_at;
        const char* name = reinterpret_cast<const char*>(base + name_at);
        size_t name_len = strnlen(name, remain);
        if (name_len == remain) {
            return false;
        }
        std::string current;
        if (!ReadCurrentValue(base, size, live, info, &current)) {
            return false;
        }
        std::string replacement;
        if (!Lookup(snap, name, &replacement) || replacement == current) {
            continue;
        }
        patches->push_back(Patch{live, name, std::move(replacement)});
    }
    return true;
}

static size_t Align4(size_t value) {
    return (value + 3) & ~size_t{3};
}

static bool PlaceLong(uint8_t* copy, size_t size, PropInfoLayout* info, uintptr_t prop,
                      uintptr_t map_start, const std::string& value, bool already_long) {
    auto* area = reinterpret_cast<PropAreaLayout*>(copy);
    if (sizeof(PropAreaLayout) > size || area->bytes_used > size - sizeof(PropAreaLayout)) {
        return false;
    }
    size_t data_size = size - sizeof(PropAreaLayout);
    size_t need = Align4(value.size() + 1);
    if (need > data_size - area->bytes_used) {
        return false;
    }
    uint8_t* dest = copy + sizeof(PropAreaLayout) + area->bytes_used;
    memcpy(dest, value.data(), value.size());
    dest[value.size()] = '\0';
    uintptr_t dest_live = map_start + static_cast<uintptr_t>(dest - copy);
    if (dest_live < prop) {
        return false;
    }
    uint32_t relative = static_cast<uint32_t>(dest_live - prop);
    if (!already_long) {
        memset(info->long_property.error_message, 0, kLongErrorSize);
        memcpy(info->long_property.error_message, kLongError, sizeof(kLongError));
        info->serial.store((static_cast<uint32_t>(sizeof(kLongError) - 1) << 24) | kLongFlag,
                           std::memory_order_relaxed);
    } else {
        // The old long bytes stay where a cached reader may still point. Move the
        // serial by 2 so that reader drops the pointer, and leave the dirty bit clear.
        uint32_t serial = info->serial.load(std::memory_order_relaxed);
        uint32_t low = ((serial + 2) & 0x00ffffffu) | kLongFlag;
        low &= ~1u;
        info->serial.store((serial & 0xff000000u) | low, std::memory_order_relaxed);
    }
    info->long_property.offset = relative;
    area->bytes_used += static_cast<uint32_t>(need);
    return true;
}

static bool PatchOne(uint8_t* copy, const Mapping& mapping, const Patch& patch) {
    size_t size = mapping.end - mapping.start;
    if (patch.prop < mapping.start ||
            patch.prop + sizeof(PropInfoLayout) > mapping.end) {
        return false;
    }
    auto* info = reinterpret_cast<PropInfoLayout*>(copy + (patch.prop - mapping.start));
    uint32_t serial = info->serial.load(std::memory_order_relaxed);
    bool is_long = (serial & kLongFlag) != 0;
    if (!is_long) {
        if (patch.value.size() >= PROP_VALUE_MAX) {
            return PlaceLong(copy, size, info, patch.prop, mapping.start, patch.value, false);
        }
        memcpy(info->value, patch.value.data(), patch.value.size());
        info->value[patch.value.size()] = '\0';
        // Mutable readers such as init.svc.adbd copy into a cache until the serial
        // changes. A same-length write still has to publish. Adding 2 matches an
        // ordinary property update and leaves the dirty bit clear.
        uint32_t low = serial & 0x00ffffffu;
        low &= ~1u;
        low &= ~kLongFlag;
        low = (low + 2) & 0x00ffffffu;
        low &= ~1u;
        low &= ~kLongFlag;
        info->serial.store((static_cast<uint32_t>(patch.value.size()) << 24) | low,
                           std::memory_order_relaxed);
        return true;
    }

    uint32_t offset = info->long_property.offset;
    uintptr_t live = patch.prop + offset;
    if (offset == 0 || live < mapping.start || live >= mapping.end) {
        return false;
    }
    size_t remain = mapping.end - live;
    char* dest = reinterpret_cast<char*>(copy + (live - mapping.start));
    size_t old_len = strnlen(dest, remain);
    if (old_len == remain) {
        return false;
    }
    if (patch.value.size() <= old_len) {
        memcpy(dest, patch.value.data(), patch.value.size());
        dest[patch.value.size()] = '\0';
        return true;
    }
    return PlaceLong(copy, size, info, patch.prop, mapping.start, patch.value, true);
}

static void ReleaseCopies(std::vector<Prepared>* prepared) {
    for (Prepared& item : *prepared) {
        if (item.copy != nullptr) {
            munmap(item.copy, item.mapping.end - item.mapping.start);
            item.copy = nullptr;
        }
    }
}

// Returns false only when a mapping was already replaced and the address space
// can no longer be left as it was.
static bool InstallCopies(std::vector<Prepared>* prepared) {
    for (Prepared& item : *prepared) {
        size_t size = item.mapping.end - item.mapping.start;
        void* placed = mmap(reinterpret_cast<void*>(item.mapping.start), size,
                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (placed != reinterpret_cast<void*>(item.mapping.start)) {
            return false;
        }
        memcpy(placed, item.copy, size);
        if (mprotect(placed, size, PROT_READ) != 0) {
            ALOGE("DroidGuard property area: mapped private copy left writable");
        }
        munmap(item.copy, size);
        item.copy = nullptr;
    }
    return true;
}

bool ApplyDroidGuardPropertyArea(const char* nice_name) {
    if (nice_name == nullptr || strcmp(nice_name, kProcessName) != 0) {
        return true;
    }
    if (!SingleThreaded()) {
        ALOGE("DroidGuard property area: specialization is not single-threaded");
        return true;
    }

    Snapshot snap;
    LoadSnapshot(&snap);
    OpenCandidateAreas(snap);

    std::vector<Mapping> maps;
    if (!ReadMaps(&maps)) {
        ALOGE("DroidGuard property area: could not read process mappings");
        return true;
    }
    std::vector<Patch> patches;
    for (const Mapping& mapping : maps) {
        if (!WalkArea(mapping, snap, &patches)) {
            ALOGE("DroidGuard property area: shared mappings left in place");
            return true;
        }
    }
    if (patches.empty()) {
        return true;
    }

    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return true;
    }
    std::vector<Prepared> prepared;
    for (const Patch& patch : patches) {
        Prepared* dest = nullptr;
        for (Prepared& item : prepared) {
            if (patch.prop >= item.mapping.start && patch.prop < item.mapping.end) {
                dest = &item;
                break;
            }
        }
        if (dest == nullptr) {
            for (const Mapping& mapping : maps) {
                if (patch.prop < mapping.start || patch.prop >= mapping.end) {
                    continue;
                }
                if ((mapping.start % static_cast<uintptr_t>(page)) != 0 ||
                        ((mapping.end - mapping.start) % static_cast<uintptr_t>(page)) != 0) {
                    ALOGE("DroidGuard property area: mapping is not page aligned");
                    return true;
                }
                prepared.push_back(Prepared{mapping, {}, nullptr});
                dest = &prepared.back();
                break;
            }
        }
        if (dest == nullptr) {
            ALOGE("DroidGuard property area: %s is outside its mapping", patch.name.c_str());
            return true;
        }
        dest->patches.push_back(patch);
    }

    for (Prepared& item : prepared) {
        size_t size = item.mapping.end - item.mapping.start;
        void* copy = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (copy == MAP_FAILED) {
            ReleaseCopies(&prepared);
            ALOGE("DroidGuard property area: could not allocate a private copy");
            return true;
        }
        item.copy = copy;
        memcpy(copy, reinterpret_cast<void*>(item.mapping.start), size);
        auto* area = reinterpret_cast<PropAreaLayout*>(copy);
        if (area->magic != kAreaMagic || area->version != kAreaVersion) {
            ReleaseCopies(&prepared);
            ALOGE("DroidGuard property area: copied area has an unexpected header");
            return true;
        }
        for (const Patch& patch : item.patches) {
            if (!PatchOne(static_cast<uint8_t*>(copy), item.mapping, patch)) {
                ReleaseCopies(&prepared);
                ALOGE("DroidGuard property area: staged value for %s does not fit, "
                      "shared mappings left",
                      patch.name.c_str());
                return true;
            }
        }
    }

    if (!InstallCopies(&prepared)) {
        ReleaseCopies(&prepared);
        ALOGE("DroidGuard property area: replacement mapping failed");
        return false;
    }
    size_t count = 0;
    for (const Prepared& item : prepared) {
        count += item.patches.size();
    }
    ALOGI("DroidGuard property area: patched %zu properties in %zu mappings", count, prepared.size());
    return true;
}
