/*
 * Copyright (C) 2020 The Pixel Experience Project
 *               2022 StatiXOS
 *               2021-2022 crDroid Android Project
 * SPDX-FileCopyrightText: Evolution X
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.android.internal.util.evolution;

import android.app.Application;
import android.content.ContentResolver;
import android.content.Context;
import android.os.Build;
import android.os.Process;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;

import java.lang.reflect.Field;
import java.util.Map;

import sun.misc.Unsafe;

/**
 * @hide
 */
public final class PixelPropsUtils {

    private static final String PACKAGE_ARCORE = "com.google.ar.core";
    private static final String PACKAGE_PHOTOS = "com.google.android.apps.photos";
    private static final String PACKAGE_SI = "com.google.android.settings.intelligence";
    private static final String PACKAGE_SNAPCHAT = "com.snapchat.android";

    private static final String TAG = PixelPropsUtils.class.getSimpleName();
    private static final boolean DEBUG = false;

    private static final String sDeviceFingerprint =
            SystemProperties.get("ro.product.fingerprint", Build.FINGERPRINT);

    private static final Map<String, Object> sPixelXLProps = Map.of(
            "BRAND", "google",
            "MANUFACTURER", "Google",
            "DEVICE", "marlin",
            "PRODUCT", "marlin",
            "HARDWARE", "marlin",
            "ID", "QP1A.191005.007.A3",
            "MODEL", "Pixel XL",
            "FINGERPRINT", "google/marlin/marlin:10/QP1A.191005.007.A3/5972272:user/release-keys"
    );

    private static volatile String sProcessName;
    private static volatile boolean sPhotosSpoofEnabled = true;
    private static volatile boolean sSnapchatSpoofEnabled = false;
    private static volatile boolean sInitialized = false;

    private static final Field OFFSET_FIELD;
    private static final Unsafe UNSAFE;

    static {
        Unsafe unsafe = null;
        Field offsetField = null;
        try {
            Field field = Unsafe.class.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            unsafe = (Unsafe) field.get(null);
            field.setAccessible(false);

            offsetField = Field.class.getDeclaredField("offset");
            offsetField.setAccessible(true);
        } catch (Throwable t) {
            Log.e(TAG, "Unable to initialize Unsafe, falling back to reflection", t);
        }
        UNSAFE = unsafe;
        OFFSET_FIELD = offsetField;
    }

    public static void init(Context context) {
        if (sInitialized || Process.isIsolated() || context == null) return;
        sInitialized = true;
        registerSpoofSettingsObserver(context);
    }

    private static void registerSpoofSettingsObserver(Context context) {
        final ContentResolver cr = context.getContentResolver();
        final Runnable refresh = () -> {
            try {
                sPhotosSpoofEnabled = Settings.Secure.getInt(
                        cr, Settings.Secure.PI_PHOTOS_SPOOF, 1) == 1;
                sSnapchatSpoofEnabled = Settings.Secure.getInt(
                        cr, Settings.Secure.PI_SNAPCHAT_SPOOF, 0) == 1;
            } catch (Throwable t) {
                // Settings provider not ready yet; cache stays at safe defaults
            }
        };
        try {
            final android.database.ContentObserver observer =
                    new android.database.ContentObserver(null) {
                @Override
                public void onChange(boolean selfChange) { refresh.run(); }
            };
            cr.registerContentObserver(
                    Settings.Secure.getUriFor(Settings.Secure.PI_PHOTOS_SPOOF), false, observer);
            cr.registerContentObserver(
                    Settings.Secure.getUriFor(Settings.Secure.PI_SNAPCHAT_SPOOF), false, observer);
        } catch (Throwable t) {
            // Observer registration failed; cached defaults remain
        }
        refresh.run();
    }

    public static void setProps(Context context) {
        if (Process.isIsolated()) {
            if (DEBUG) Log.d(TAG, "Skipping setProps in isolated process");
            return;
        }

        final String packageName = context.getPackageName();
        final String processName = Application.getProcessName();
        if (packageName == null || processName == null || packageName.isEmpty()) {
            return;
        }

        sProcessName = processName;
        init(context);

        setPropValue("TYPE", "user");
        setPropValue("TAGS", "release-keys");

        if (packageName.equals(PACKAGE_SI)) {
            setPropValue("FINGERPRINT", String.valueOf(Build.TIME));
            return;
        }
        if (packageName.equals(PACKAGE_ARCORE)) {
            setPropValue("FINGERPRINT", sDeviceFingerprint);
            return;
        }
        if (packageName.equals(PACKAGE_PHOTOS)) {
            if (sPhotosSpoofEnabled) {
                sPixelXLProps.forEach(PixelPropsUtils::setPropValue);
            }
            return;
        }
        if (packageName.equals(PACKAGE_SNAPCHAT) && sSnapchatSpoofEnabled) {
            sPixelXLProps.forEach(PixelPropsUtils::setPropValue);
        }
    }

    public static void setPropValue(String key, Object value) {
        Field field = null;
        try {
            field = getBuildClassField(key);
            if (field == null) {
                Log.e(TAG, "Field " + key + " not found in Build or Build.VERSION classes");
                return;
            }
            field.setAccessible(true);

            Object coerced;
            if (field.getType() == int.class) {
                coerced = (value instanceof String)
                        ? Integer.parseInt((String) value)
                        : (Integer) value;
            } else if (field.getType() == long.class) {
                coerced = (value instanceof String)
                        ? Long.parseLong((String) value)
                        : (Long) value;
            } else {
                coerced = value.toString();
            }

            if (UNSAFE != null && OFFSET_FIELD != null) {
                try {
                    int offset = OFFSET_FIELD.getInt(field);
                    if (field.getType() == int.class) {
                        UNSAFE.putInt(field.getDeclaringClass(), offset, (Integer) coerced);
                    } else if (field.getType() == long.class) {
                        UNSAFE.putLong(field.getDeclaringClass(), offset, (Long) coerced);
                    } else {
                        UNSAFE.putObject(field.getDeclaringClass(), offset, coerced);
                    }
                    dlog("Set prop " + key + " to " + value + " via Unsafe");
                    return;
                } catch (Throwable t) {
                    dlog("Unsafe set failed for " + key + ", falling back to reflection: " + t.getMessage());
                }
            }

            field.set(null, coerced);
            dlog("Set prop " + key + " to " + value + " via reflection");
        } catch (NoSuchFieldException | IllegalAccessException | IllegalArgumentException e) {
            Log.e(TAG, "Failed to set prop " + key, e);
        } finally {
            if (field != null) {
                try {
                    field.setAccessible(false);
                } catch (Exception ignored) {}
            }
        }
    }

    private static Field getBuildClassField(String key) throws NoSuchFieldException {
        try {
            Field field = Build.class.getDeclaredField(key);
            dlog("Field " + key + " found in Build.class");
            return field;
        } catch (NoSuchFieldException e) {
            Field field = Build.VERSION.class.getDeclaredField(key);
            dlog("Field " + key + " found in Build.VERSION.class");
            return field;
        }
    }

    public static void dlog(String msg) {
        if (DEBUG) Log.d(TAG, "[" + sProcessName + "] " + msg);
    }
}
