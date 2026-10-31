# Phase 6 installer acceptance

Build the offline package with [installer/BUILD.md](../installer/BUILD.md).
Build success and automated checks do not certify a clean-machine install or
public redistribution readiness.

## Results recorded 21–22 October 2026

- MSVC 2022 / Qt 6.12.0 Release application and test targets built.
- CTest: **10/10 suites passed** (including `launch_channel` and
  `whpx_script`); the run after the startup-window change took 32.52 seconds.
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
- The package preflight verified all **3,399** staged paths and hashes.
- Latest local setup artifact: `installer/Output/DroidBoxSetup.exe`,
  **1,810,387,794 bytes**, SHA-256
  `057e2ecfd75986d81d1d7328328f77c48e2a244c04204b9472d41a8446ad7055`.
  Generated installer output is ignored by Git and is not part of the source
  repository.

### Latest rebuilt installer run (22 October)

- The exact setup artifact above completed a silent in-place upgrade at
  `%LOCALAPPDATA%\DroidBoxAcceptance\Final Install`, a path containing spaces.
  Inno logged `Installation process succeeded`; the bundled VC++ runtime needed
  no restart. The package preflight had verified all **3,399** staged files.
- The Start Menu and Public Desktop shortcuts exist, and the machine `.apk`
  command points to the installed executable. The installed base image is
  4,136,566,784 bytes and its SHA-256 (`f46bc328…838a090d`) matches the bundle
  manifest.
- Launching the upgraded app started QEMU from the new install directory and
  embedded the guest display. The existing profile referred to a different,
  older parallel install path; I backed it up and changed only `qemuPath` to
  this install before launch. This test therefore does not verify automatic
  migration of a profile when the install directory changes.
- The installed app window opens with a 900-pixel client width (915 pixels
  outer width on this desktop). ADB reached `device`; the guest reported
  `sys.boot_completed=1`, Android 11, and the Bliss launcher. A screenshot
  confirmed the Android Home screen. The existing overlay remained usable.
- This account had prior DroidBox data and an authorized ADB key. These results
  do not prove first-run RSA authorization or clean-account overlay creation.

These checks were performed on one already-configured PC, with network access
available. A clean, disconnected Windows machine and a second Windows account
were unavailable, so offline first-run behavior on an unconfigured system is
not established. The in-place installer upgrade succeeded, but a separate
application-data A/B retention check is still outstanding. Public redistribution
remains blocked: `installer/release-inventory.json` has not been reviewed or
created, and the source, notices, and license inventory must be completed as
described in [installer/BUILD.md](../installer/BUILD.md). The local setup EXE is
ignored by Git and has not been published.

## Version 2 release-candidate follow-up — 31 October 2026

- The 27 October `SabiSetup.exe` completed at 1,810,465,339 bytes with SHA-256
  `dc4f571c1c86f1f8c6a813bdae05eff6563d1cd8e80fc4162526fe3da2c31a6c`.
  It is structurally complete; the earlier 443 MB output was an interrupted
  Inno Setup compression result and must not be used.
- A physical touchpad test exposed a cursor-boundary defect in the embedded
  SDL display: the low-level hook suppressed `WM_MOUSEMOVE`. The release
  candidate now lets motion continue to Windows and consumes only successful
  QMP button transitions. The rebuilt installed application was physically
  tested: the cursor entered the Android display and touchpad click/drag
  operation worked while toolbar controls remained reachable.
- `display_input` and `native_keyboard` passed after that fix. The complete
  CTest run and installer below must be regenerated from the final committed
  release-candidate source before distribution.
- The final release-candidate rebuild completed from commit `94900e5` after
  a fresh bundle manifest verified 3,399 payload paths and hashes. Its output
  is `installer/Output/SabiSetup.exe`, 1,810,483,824 bytes, SHA-256
  `8186ca60d6de7fce7f3f3c526e66f5a177fc5556de75799bd85f33d010eafdb1`.
  The staged `Sabi.exe` SHA-256 matched the tested build
  (`70e274a59d1ab67ffc0f0a6276b0b93e348e6d1e667a5890d3ef455577ae46ca`).
  Inno Setup exited successfully and the wrapper reported no errors.

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
- [x] Upgrade the installer in place; verify the retained overlay boots.
- [ ] Verify custom apps, guest data, and settings survive an A/B upgrade.
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
