# Phase 6 manual acceptance

Build with [installer/BUILD.md](../installer/BUILD.md). Compiler success does not
certify installation, boot, or redistribution readiness. `tests/test_installer.ps1`
is a static packaging/payload preflight; it never installs software or changes
Windows features. Run it explicitly when reviewing packaging.

## Build record — 21 October 2026

- Release host build: passed (MSVC 2022, Qt 6.12.0).
- Payload staging and pinned file/source hash checks: passed.
- Inno Setup 6.7.3: successful compile, exit 0.
- Output: `installer/Output/DroidBoxSetup.exe`, **1,810,419,303 bytes**;
  the output directory contains no external payload files.
- SHA-256: `503b8ac190d848a0ce4b02495abcea2d02585702b696a4cde45cf6c74a84ddc1`.
- The installer has not been run. Automated validation results are recorded below.
  Installation, WHPX runtime checks, Android boot and uninstall remain manual
  acceptance items below. Public distribution still requires the source inventory
  review in `installer/BUILD.md`; this build records `releaseReady=false`.

## Automated validation — 21 October 2026

- Configured `cmake-build-phase6-fixes` with `BUILD_TESTING=ON` and rebuilt all
  Release application/test targets: **passed**.
- `ctest --test-dir cmake-build-phase6-fixes -C Release --output-on-failure`:
  **8/8 passed, zero failures**, 21.20 seconds. Suites: installer_preflight,
  vm_control_config, display_input, keymap, adb, instance_manager, phase5_ui,
  native_keyboard. Qt 6.12.0's `bin` directory was added to this process's PATH.
- `tests/test_installer.ps1 -BundlePath "$PWD/installer/bundle"`: **passed**;
  all **3,397** staged file paths and SHA-256 hashes matched the manifest.
- Rechecked the existing setup EXE: its size and SHA-256 match the successful
  compiler build recorded above. The unchanged installer was not recompressed.
- No implementation failures were found in these checks; no code fixes were needed.
- Full installation/uninstallation and elevated WHPX checks were not executed.
  This session is not elevated, approximately 5 GB of disk space is available,
  and an existing development DroidBox/QEMU session is running. These results
  do not complete the clean-machine, offline or guest acceptance checklist.

## Build/package

- [x] Release host builds with dynamic Qt.
- [x] Staging accepts the reviewed clean image and matching source archives.
- [x] One setup EXE is produced, with no external `.bin` files or downloads.
- [ ] License screen and installed full texts/notices/source offer are present.
- [ ] Record setup SHA-256; complete the source inventory before public distribution.

## Clean Windows 10/11, networking disconnected

- [ ] Copy only setup.exe from USB; install succeeds offline.
- [ ] Use an install directory containing spaces.
- [ ] Install as a standard user accepting UAC, including another administrator's credentials.
- [ ] Original user's LocalAppData contains default config, overlay and versioned backing base.
- [ ] Start Menu and selected Desktop shortcut launch the installed app.
- [ ] Android reaches Home; new host receives the RSA prompt.
- [ ] Allow reaches Ready without restarting or repeated adb connect calls.
- [ ] Home, Back, Screenshot and APK install work.
- [ ] APK double-click preserves an exact path containing spaces.
- [ ] Existing Windows per-user APK choice is respected; Open With offers DroidBox.
- [ ] Reinstall/upgrade preserves apps, data and configurations.
- [ ] Another Windows account receives its own default instance on first launch.

## WHPX states

- [ ] Disabled feature offers enablement; declining leaves it disabled.
- [ ] Accepting enablement uses local component files and reports errors honestly.
- [ ] BIOS virtualization disabled shows VT-x/SVM instructions.
- [ ] Active Hyper-V does not cause a false BIOS-disabled conclusion from CIM.
- [ ] Actual diskless QEMU WHPX probe succeeds on capable hardware.
- [ ] Restart/error states suppress automatic Android launch.
- [ ] No forced restart; `/NORESTART` is respected.

A Windows VM used for boot testing needs usable nested virtualization. Otherwise
perform the WHPX/Android check on physical Windows hardware.

## Uninstall

- [ ] Close installed DroidBox and QEMU before uninstalling.
- [ ] No to erasure preserves instances **and backing bases**.
- [ ] Reinstall elsewhere and boot the retained instances using the current QEMU.
- [ ] Yes removes only the current uninstaller account's DroidBox data.
- [ ] Other accounts/unrelated files are preserved.
- [ ] Junctions and open writers prevent unsafe erasure and produce a useful message.
- [ ] Silent uninstall preserves user data.
- [ ] Shortcuts, ProgID and Open With registration are removed.
- [ ] Previous machine APK default is restored only while it still points to DroidBox.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1 -BundlePath "$PWD/installer/bundle"
# Elevated, installed location:
powershell -NoProfile -ExecutionPolicy Bypass -File 'C:/Program Files/DroidBox/check_whpx.ps1' -Probe
Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform
```

Expected WHPX: JSON status `Ready`, `probePassed=true`, exit 0. Other codes:
10 feature disabled, 11 restart required, 12 firmware disabled, 20 query/probe
error. Inspect the message; a feature flag alone does not certify VM boot.
