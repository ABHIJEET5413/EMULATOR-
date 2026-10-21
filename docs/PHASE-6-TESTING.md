# Phase 6 installer acceptance

Build the offline package with [installer/BUILD.md](../installer/BUILD.md).
Build success and automated checks do not certify a clean-machine install or
public redistribution readiness.

## Results recorded 21 October 2026

- MSVC 2022 / Qt 6.12.0 Release application and test targets built.
- CTest: **10/10 suites passed** (including `launch_channel` and
  `whpx_script`); the run took 35.11 seconds.
- Instance manager regression harness: **45 checks passed**, including
  wildcard IPv4/IPv6 port collision detection.
- WHPX PowerShell regression tests: passed, covering direct `-File` invocation
  and mocked ready, disabled, restart, firmware, query-error, and enablement
  cases without changing Windows features.
- Elevated hardware probe on this PC returned `Ready`,
  `probePassed=true`, and exit code 0.
- A silent install to a directory containing spaces completed on this PC. The
  installed app booted Bliss to Android Home, completed ADB authorization, and
  reported `sys.boot_completed=1`.
- Live ADB checks passed APK installation, file push, media scan, PNG screenshot
  capture/decoding, and Android navigation key events against that guest.
- The package preflight verified all **3,397** staged paths and hashes.
- Latest local setup artifact: `installer/Output/DroidBoxSetup.exe`,
  **319,804,420 bytes**, SHA-256
  `64b42ac38280f2afcc251c38779a3f8188e127524cb5b34d2d9faa760f928448`.
  Generated installer output is ignored by Git and is not part of the source
  repository.

These checks were performed on one already-configured PC. They do not establish
that installation works on a clean, disconnected Windows machine. The acceptance
installer was removed after testing to recover disk space. A/B upgrade and
retained user-data checks were not completed. Public redistribution remains
blocked on completing the source, notices, and license inventory described in
[installer/BUILD.md](../installer/BUILD.md).

## Remaining manual acceptance

### Clean Windows 10/11, network disconnected

- [ ] Install using only the single setup EXE; verify an install path containing spaces.
- [ ] Install as a standard user through UAC.
- [ ] Confirm the original user's LocalAppData receives a config, overlay, and
  versioned backing base.
- [ ] Start Menu and optional Desktop shortcuts start the app.
- [ ] Verify the guest reaches Home, ADB authorizes, and the app reaches Ready.
- [ ] Verify Home, Back, Screenshot, APK install, and APK paths containing spaces.
- [ ] Check existing per-user `.apk` choice and Open With behavior.
- [ ] Upgrade in place and verify apps, guest data, and settings are retained.
- [ ] Install under another Windows account and confirm its instance data is separate.

### WHPX states

- [ ] Disabled feature offers enablement; declining leaves it disabled.
- [ ] Accepting enablement uses local component files and reports errors honestly.
- [ ] Disabled BIOS virtualization gives VT-x/SVM guidance.
- [ ] Active Hyper-V does not produce a false firmware-disabled result.
- [ ] Restart and error states do not automatically launch Android.
- [ ] No forced restart occurs; `/NORESTART` is respected.

### Uninstall

- [ ] Close DroidBox and QEMU before uninstalling.
- [ ] Verify the chosen keep/delete option affects only the current account's data.
- [ ] Reinstall elsewhere and boot retained instances with the bundled QEMU.
- [ ] Other accounts and unrelated files remain untouched.
- [ ] Junctions and open writers block unsafe data removal with a useful message.
- [ ] Silent uninstall preserves user data.
- [ ] Shortcuts, ProgID, Open With registration, and prior `.apk` default are cleaned
  up safely.

## Commands

```powershell
cmake -B cmake-build-phase6-fixes -S host-app -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64" -DBUILD_TESTING=ON
cmake --build cmake-build-phase6-fixes --config Release
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
ctest --test-dir cmake-build-phase6-fixes -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1 `
  -BundlePath "$PWD/installer/bundle"
```

Elevated WHPX verification from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File installer/check_whpx.ps1 `
  -Probe -QemuPath "$PWD/vm/qemu/qemu-system-x86_64.exe"
Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform
```

Expected probe result: JSON status `Ready`, `probePassed=true`, exit 0. Other
script exit codes: 10 feature disabled, 11 restart required, 12 firmware
virtualization disabled, 20 query or probe error. A feature flag alone does not
certify that Android boots.
