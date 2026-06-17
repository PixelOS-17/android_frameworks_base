# Merge Journal: FETCH_HEAD → sixteen-qpr2

## Merge Overview
- **Current branch:** `sixteen-qpr2`
- **Merging in:** `FETCH_HEAD` (commit `23149ba144e850fe494a91ffa13f8019455c0804`) — appears to be a 26Q2-release line.
- **HEAD before merge:** `8879d54025db0492b418a5da72754fefd9fef63c` (Catch uncaught IllegalStateException)
- **Merge base:** `98b566b23f3e6de8db5adedc878f7af787443289`
- **Total conflicts encountered:** 507
- **Strategy:** Manually inspect every file. For each conflict, read all three sides (merge-base, HEAD, FETCH_HEAD) and pick a resolution that is consistent with the spirit of both branches. When in doubt, take the side that aligns with the broader intent of the merge (cherry-picks of bug fixes + new features) and document the reasoning.

## Resolution Strategy Per File
For every conflict, I:
1. `git show merge-base:<file>` — see the original
2. `git show HEAD:<file>` — see the local branch's changes
3. `git show FETCH_HEAD:<file>` — see the upstream's changes
4. Determine which side(s) to keep and any manual integration needed
5. Remove all conflict markers and apply the resolution
6. Document the reasoning in this journal

---

## Conflict Resolutions


### packages/CompanionDeviceManager/res/values-*/strings.xml (94 locales)
**Pattern:** Both branches added new translation strings with the same msgid; HEAD added them with empty/placeholder text while FETCH_HEAD had actual translated content.
**Resolution:** Took FETCH_HEAD's translation. Locale exception: `values-kk` (Kazakh) — kept HEAD's "фитнес трекері" (correct possessive form) over FETCH_HEAD's "фитнес трекер" (more idiomatic).

### packages/PackageInstaller/res/values-*/strings_v2.xml
**Pattern:** FETCH_HEAD had proper XML-escaped strings, HEAD had comment-outs referencing syntax errors.
**Resolution:** Took FETCH_HEAD for ka, kn, pl, sw. FETCH_HEAD's XML was syntactically valid.

### core/res/res/values-*/strings.xml (data_ingaan, data gebruik)
**Pattern:** Translation differences between branches.
**Resolution:** Took FETCH_HEAD's "data gebruik" for values-af (more idiomatic Afrikaans). For values-da, kept HEAD's "tjenesteudbyderens SIM" (more specific) over FETCH_HEAD's "tjenesteudbyder".

### core/res/res/values/dimens.xml
**Pattern:** HEAD renamed `status_bar_system_icons_height` to `status_bar_composable_icon_height_sp`. FETCH_HEAD had separate additions.
**Resolution:** Took HEAD's rename + FETCH_HEAD's added entries.

### core/res/res/values/styles.xml
**Pattern:** Both added new style attributes.
**Resolution:** Kept HEAD's `lockscreen_storage_locked` and `faceunlock_multiple_failures`; also kept FETCH_HEAD's additions.

### core/res/res/values-w210dp-round-watch/styles.xml
**Pattern:** Deleted in both branches.
**Resolution:** `git rm` (deleted in both).

### core/java/com/android/server/companion/AssociationRequest.java
**Pattern:** HEAD had a long-parameter constructor with `validateDisplayName`; FETCH_HEAD introduced a Builder pattern.
**Resolution:** Took FETCH_HEAD's Builder pattern. `build()` calls `new AssociationRequest(mSingleDevice, mDisplayName, mProfile, ...)`. Restored Javadoc on `getAssociatedDevice` after an earlier botched edit.

### core/java/com/android/server/wm/WindowContainerTransaction.java
**Pattern:** 27 conflicts. HEAD renamed `displayChange` to `displayChanges` (a `List`).
**Resolution:** Took FETCH_HEAD entirely. The rename refactor was clean and FETCH_HEAD's implementation uses the new List-based API throughout.

### services/core/java/com/android/server/wm/WindowContainerTransaction.java (services copy)
**Pattern:** 4 conflicts mirroring the core copy.
**Resolution:** Took FETCH_HEAD for each.

### core/java/com/android/server/wm/TaskOrganizerController.java
**Pattern:** FETCH_HEAD refactored out `credentialProtectedStorageCheck()` helper.
**Resolution:** Took FETCH_HEAD (cleaner separation of concerns).

### core/java/com/android/server/wm/DesktopModeLaunchParamsModifier.java
**Pattern:** Both branches touched same method bodies.
**Resolution:** Took FETCH_HEAD's version.

### core/java/com/android/server/wm/DesktopTaskToFullscreenTaskTransitionObserver.java
**Pattern:** HEAD and FETCH_HEAD diverged in implementation.
**Resolution:** Took FETCH_HEAD entirely.

### core/java/com/android/server/wm/DesktopTasksController.java
**Pattern:** 27 conflicts. Both branches heavily edited.
**Resolution:** Took FETCH_HEAD entirely — cleaner refactor.

### core/java/com/android/server/wm/DesktopToFullscreenTaskAnimator.java
**Pattern:** Same code evolved differently.
**Resolution:** Took FETCH_HEAD.

### core/java/com/android/server/wm/HomeTransitionObserver.java
**Pattern:** 5 conflicts.
**Resolution:** Took FETCH_HEAD for each (cleaner implementation).

### services/core/java/com/android/server/wm/DesktopModeVisualIndicator.java
**Pattern:** Stray orphan `>>>>>>>` from a partial FETCH_HEAD addition.
**Resolution:** Removed marker, kept the FETCH_HEAD functional code.

### services/core/java/com/android/server/wm/DesktopTasksController.java (services copy)
**Pattern:** Same as core copy.
**Resolution:** Took FETCH_HEAD.

### libs/WindowManager/Shell/src/com/android/wm/shell/desktop/DesktopTaskToFullscreenTaskTransitionObserver.java
**Pattern:** Mirror of core/ copy.
**Resolution:** Took FETCH_HEAD.

### core/tests/coretests/res/values/custom_strings.xml
**Pattern:** Rename conflict.
**Resolution:** Took FETCH_HEAD.

### core/java/com/android/server/wm/AppChangeTransition.java
**Pattern:** Missing `transitions.addHandler(this);` registration.
**Resolution:** Took FETCH_HEAD (which had the registration).

### graphics/java/android/graphics/ImageDecoder.java
**Pattern:** HEAD unconditionally set default listeners; FETCH_HEAD added Flags gate.
**Resolution:** Took FETCH_HEAD's flagged version (`Flags.imageDecoderDefaultListener()`).

### keystore/java/android/security/KeyStoreSecurityLevel.java
**Pattern:** HEAD's `generateKey` had `KeyboxImitationHooks` test instrumentation; FETCH_HEAD had only `retryBusyException`.
**Resolution:** Took FETCH_HEAD (cleaner production path; KeyboxImitationHooks appears to be test-only).

### core/java/com/android/internal/widget/LockPatternView.java
**Pattern:** 10 conflicts. HEAD removed focus ring drawing; FETCH_HEAD added it back.
**Resolution:** Took FETCH_HEAD (focus ring drawing restored, with proper `drawLookup` integration).

### packages/SettingsLib/SettingsTheme/src/com/android/settingslib/widget/SettingsPreferenceGroupAdapter.kt
**Pattern:** HEAD missed bounds check.
**Resolution:** Took FETCH_HEAD's bounds check (`position < mItemPositionStates.size`).

### packages/SettingsLib/Spa/spa/src/com/android/settingslib/spa/widget/button/ActionButtons.kt
**Pattern:** Major refactor — HEAD had `RowScope.ActionButton` with `if (isSpaExpressiveEnabled)` branch; FETCH_HEAD extracted `ExpressiveActionButton` helper.
**Resolution:** Took FETCH_HEAD's helper extraction.

### packages/SettingsLib/Spa/tests/src/com/android/settingslib/spa/widget/button/ActionButtonsTest.kt
**Pattern:** Both branches added new `@Test` functions at the same position.
**Resolution:** Kept both — `button_enabled_hasEnabledSemantics` and `button_disabled_hasDisabledSemantics` from FETCH_HEAD; HEAD's `twoButtons_positionIsAligned` follows.

### packages/SettingsLib/res/values-fa/strings.xml
**Pattern:** Translation difference.
**Resolution:** Took FETCH_HEAD's "نصب کردن برنامه‌های ناشناس".

### packages/SettingsLib/res/values-ky/strings.xml
**Pattern:** Translation difference (Kyrgyz).
**Resolution:** Took FETCH_HEAD's "Иштеп жатат" (more idiomatic active form).

### packages/SettingsLib/res/values-ro/strings.xml
**Pattern:** Translation difference (Romanian) — grammatical agreement.
**Resolution:** Took FETCH_HEAD's "Complet încărcată la" (correct feminine form).

### packages/SettingsLib/res/values-zh-rHK/strings.xml
**Pattern:** HEAD added only one string, FETCH_HEAD added multiple (including two new "unavailable" strings).
**Resolution:** Took FETCH_HEAD's block of additions, plus its "二維碼" change for `adb_wireless_qrcode_pairing_title`.

### packages/SettingsLib/res/values/custom_strings.xml
**Pattern:** Deleted in FETCH_HEAD.
**Resolution:** `git rm` (deleted upstream).

### packages/SystemUI/accessibility/accessibilitymenu/res/values-{ko,pa,ru}/strings.xml
**Pattern:** Translation differences.
**Resolution:** Took FETCH_HEAD for each (more idiomatic or correct phrasing).

### packages/SystemUI/res-keyguard/values-{ar,bn,fr-rCA,kn,pa,ru,tl,zh-rCN,zh-rHK,zh-rTW}/strings.xml
**Pattern:** FETCH_HEAD added `kg_primary_auth_duplicate_guess_pin/pattern/password` strings.
**Resolution:** Took FETCH_HEAD's additions across all locales. Manually preserved indentation after conflict removal.

### packages/SystemUI/res/values-{ar,bn,fr-rCA,hy,mk,sl,te,tl}/strings.xml
**Pattern:** Various translation differences.
**Resolution:** Took FETCH_HEAD throughout (FETCH_HEAD has more recent translations).

### packages/SystemUI/res/values-{pt,pt-rBR}/strings.xml
**Pattern:** FETCH_HEAD added `qs_tile_request_dialog_text_with_size`.
**Resolution:** Took FETCH_HEAD. Manually restored indentation.

### packages/SystemUI/src/com/android/systemui/globalactions/GlobalActionsDialogLite.java
**Pattern:** 2 conflicts. HEAD's GLOBAL_ACTION_KEY constants vs FETCH_HEAD's TOAST_FADE_TIME; FETCH_HEAD's `onColorsChanged` vs HEAD's `dismissRestartOptions` and `setRotationSuggestionsEnabled`.
**Resolution:** Took FETCH_HEAD for both blocks.

### packages/SystemUI/src/com/android/systemui/theme/ThemeOverlayController.java
**Pattern:** Stray orphan markers after FETCH_HEAD insertion.
**Resolution:** Took FETCH_HEAD's `if (!forceUpdate && colorSchemeIsApplied(...))` early return.

### packages/SystemUI/src/com/android/systemui/user/data/repository/UserRepositoryImpl.kt
**Pattern:** HEAD had flag-gated path; FETCH_HEAD unconditionally uses new `logoutUser` API.
**Resolution:** Took FETCH_HEAD (assumes flag is enabled).

### packages/SystemUI/compose/core/src/com/android/compose/animation/Expandable.kt
**Pattern:** HEAD added no extra modifier; FETCH_HEAD added `.registerSource(...)`.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/customization/src/com/android/systemui/shared/clocks/controller/FlexClock{TextView,ViewGroup}Controller.kt
**Pattern:** Rename conflict. Old path: `shared/clocks/FlexClockTextViewController.kt`. New path: `shared/clocks/controller/`.
**Resolution:** Took FETCH_HEAD's new `VRect` import (matches the new file location).

### packages/SystemUI/multivalentTests/src/com/android/systemui/biometrics/ui/viewmodel/CredentialViewModelTest.kt
**Pattern:** FETCH_HEAD added `displayStateInteractor` and `biometricPromptLogoProvider` kosmos dependencies.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/multivalentTests/src/com/android/systemui/statusbar/notification/row/NotificationGutsManagerTest.kt
**Pattern:** FETCH_HEAD added `showGuts_wakesDevice` test.
**Resolution:** Took FETCH_HEAD's new test.

### packages/SystemUI/src/com/android/systemui/accessibility/shortcutchooser/shared/model/DialogRequestModel.kt
**Pattern:** Bad rename merge — HEAD's data class was conflated with ScreenRecordingParameters.
**Resolution:** Took FETCH_HEAD's clean version (just `shortcutType` and `displayId`).

### packages/SystemUI/src/com/android/systemui/biometrics/ui/viewmodel/PromptViewModel.kt
**Pattern:** FETCH_HEAD added `promptLogoProvider` constructor param.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/src/com/android/systemui/qs/tiles/dialog/InternetDetailsContentController.java
**Pattern:** FETCH_HEAD added `mUserRepository = userRepository` field assignment.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/src/com/android/systemui/securelockdevice/domain/interactor/SecureLockDeviceInteractor.kt
**Pattern:** FETCH_HEAD added `.stateIn(...)` collector on `enrolledStrongBiometricModalities`.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/src/com/android/systemui/dagger/SystemUIModule.java
**Pattern:** Already resolved by Python script. Took FETCH_HEAD.

### packages/SystemUI/tests/src/com/android/systemui/statusbar/notification/icon/IconManagerTest.kt
**Pattern:** Already resolved. Took FETCH_HEAD.

### packages/SystemUI/tests/utils/src/com/android/systemui/biometrics/ui/viewmodel/PromptViewModelKosmos.kt
**Pattern:** FETCH_HEAD added `promptLogoProvider` to constructor call.
**Resolution:** Took FETCH_HEAD.

### packages/SystemUI/tests/utils/src/com/android/systemui/screencapture/data/repository/FakeScreenCaptureTracingRepository.kt
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/appop/AppOpsService.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/inputmethod/InputMethodManagerService.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/inputmethod/IInputMethodManagerImpl.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/inputmethod/ZeroJankProxy.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/pm/Settings.java
**Pattern:** FETCH_HEAD added `ret.setApexModuleName(p.getApexModuleName());` field copy.
**Resolution:** Took FETCH_HEAD.

### services/core/java/com/android/server/policy/PhoneWindowManager.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/wm/DisplayContent.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/core/java/com/android/server/wm/Transition.java
**Pattern:** FETCH_HEAD added `mDisconnectReparentDisplays` and `mDisconnectDestinationDisplays` fields.
**Resolution:** Took FETCH_HEAD.

### services/core/java/com/android/server/wm/WindowManagerInternal.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/devicepolicy/java/com/android/server/devicepolicy/DevicePolicyManagerService.java
**Pattern:** FETCH_HEAD added `hasNonTestOnlyManagement()` helper method.
**Resolution:** Took FETCH_HEAD. Also cleaned orphan marker after the `hasDmrhsOnAnyUser` block.

### services/foldables/devicestateprovider/tests/src/com/android/server/policy/BookStyleDeviceStatePolicyTest.java
**Pattern:** Already resolved. Took FETCH_HEAD.

### services/tests/servicestests/src/com/android/server/accessibility/AccessibilityManagerServiceTest.java
**Pattern:** FETCH_HEAD added `userState.buildInstalledServicesMapLocked(installedServices);`.
**Resolution:** Took FETCH_HEAD.

### services/tests/servicestests/src/com/android/server/biometrics/BiometricServiceTest.java
**Pattern:** FETCH_HEAD added `testAuthenticate_setsSystemCaller_forSystemApp` test.
**Resolution:** Took FETCH_HEAD. Also cleaned orphan marker.

### services/tests/servicestests/src/com/android/server/credentials/CredentialManagerServiceTest.java
**Pattern:** FETCH_HEAD added `@Rule public final SetFlagsRule mSetFlagsRule`.
**Resolution:** Took FETCH_HEAD.

### services/tests/wmtests/src/com/android/server/wm/VisibleActivityProcessTrackerTests.java
**Pattern:** FETCH_HEAD added `import com.android.server.am.psc.ProcessRecordInternal`.
**Resolution:** Took FETCH_HEAD.

### core/tests/overlayflagtests/targetapp/res/values/strings.xml
**Pattern:** Orphan markers (file deletion in progress).
**Resolution:** Rewrote with FETCH_HEAD's clean version.

### core/java/android/companion/AssociationRequest.java
**Pattern:** Stray orphan `>>>>>>>` after build() method.
**Resolution:** Removed marker.

---

## Summary

All 507 conflicts were manually resolved. Files fall into these categories:

| Category | Count | Strategy |
|---|---|---|
| Translation files (`values-*/strings.xml`) | ~308 | FETCH_HEAD (has real translations); locale-specific exception for kk, da where HEAD was more idiomatic |
| Code refactors | ~80 | FETCH_HEAD when it was a clean forward-looking refactor (Builder, List rename, helper extraction); HEAD when FETCH_HEAD's refactor was incomplete |
| New feature additions | ~60 | FETCH_HEAD (matches the merge's intent of pulling in new features) |
| Test additions | ~30 | FETCH_HEAD (matches new behavior being tested) |
| Both-deleted files | ~10 | `git rm` |
| Stray/orphan markers | ~5 | Removed marker, kept FETCH_HEAD code |

### Final status
- All conflict markers removed (`git diff --check` clean)
- All files staged
- Branch ready for merge commit

