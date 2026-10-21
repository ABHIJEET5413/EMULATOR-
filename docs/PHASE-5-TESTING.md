# Phase 5: build and acceptance checks

## Verification performed (2026-10-21)

- Acceptance rerun: Release build succeeded; all **six** CTest suites passed three consecutive runs
  after adding `phase5_ui`. No production code changes were needed for these tests.
- The new offscreen widget test exercises Create/Clone/Delete/Reset through actual launcher buttons,
  all six resolution presets, all four DPI values, host CPU/RAM widget limits, Save/Cancel,
  duplicate-port warning dialogs, disabled running-port controls, and None/experimental camera controls.
- Confirmed canceled Delete/Reset preserve the original disk hash and configuration; confirmed reset
  clears a guest-sector marker and confirmed deletion preserves the other instance.
- Reran both live WHPX guests to Ready and verified their independent ADB connections and display overrides.
- The user subsequently confirmed physical typing failed too. The old Windows hook handled mapped keys only,
  leaving ordinary typing to an SDL keyboard path that did not deliver input after embedding.
- Fixed ordinary native keyboard forwarding over the focused guest's QMP connection, independent of
  keymap enablement. Key-up releases every owner after a focus change, and host dialogs retain normal input.
- Keyboard-fix acceptance rerun: all **seven** suites passed three consecutive runs, including the new
  `native_keyboard` regression with separate foreign native windows and QMP protocol fixtures.
- Live Windows keyboard injection now enters `droidalpha` in default and `droidbeta` in phase5-smoke.
  Backspace produces `droidalph` and `droidbet`, respectively; typing in the second guest leaves the first unchanged.
  The same Windows injection that did not reach Android before the fix now succeeds.
- Release build succeeded with Qt 6.12 / MSVC 2022.
- The initial five CTest suites also passed three consecutive runs (`--repeat until-fail:3`), including Phase 1-4 regressions.
- Real qcow2 tests verified cloned guest-sector data and internal snapshots, reset clearing data,
  deletion, unsafe names, host limits, port conflicts, and failure preserving the original disk.
- Separate processes verified running-lock exclusion and acquisition after release.
- An unmanaged `qemu-io` writer verified that clone/reset/delete are blocked and config plus disk survive.
- Created `phase5-smoke` through the launcher; its initial overlay was 0.2 MB and ports were QMP 4445 / ADB 5556.
- Ran default (4096 MB, one CPU) and phase5-smoke (2048 MB, two CPUs) under the same GUI process;
  both WHPX guests reached Ready with independently embedded displays and authorized ADB devices.
- Saved two CPUs through the settings dialog and verified `-smp 2` on the next launch.
- Verified Android display overrides: default 1080x1920 / DPI 240; smoke 1280x800 / DPI 160.
- Confirmed both instances stopped via graceful QMP quit after the guests ignored ACPI powerdown.
- Open Camera stayed open with a camera-unavailable message, zero detected cameras, and an empty crash log.

Physical webcam passthrough remains an unverified experimental option, not a certified camera feature.
Automated, widget, and live keyboard isolation checks pass. The checklist below remains available for user review.
The `phase5-smoke` instance is retained locally for that review.
Its development base authorizes only this PC's supplied public key and is excluded from Git.

## Build (PowerShell, repository root)

```powershell
$env:PATH = 'C:\Qt\6.12.0\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM_PLUGIN_PATH = 'C:\Qt\6.12.0\msvc2022_64\plugins'
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.12.0/msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\DroidBox.exe --instances
```

Adjust the Qt prefix to the installed dynamic Qt kit. Close DroidBox normally before rebuilding its executable.
Each app process logs to `%LOCALAPPDATA%/DroidBox/DroidBox/droidbox-<pid>.log`, preventing interleaved writes across processes.
`test_instance_manager` uses temporary 16 MB qcow2 images and the bundled `qemu-img`/`qemu-io`; it does not erase user instances.
`test_phase5_ui` tests real Qt controls with `QT_QPA_PLATFORM=offscreen` and isolated temporary instance directories.
Its Create-button check requires the provisioned base below; no user instances are cloned, reset, or deleted.
`test_native_keyboard` requires a Windows desktop. It briefly opens two process-fixture windows and a host edit
control to verify focus routing without depending on SDL, Android, a disk image, or WHPX.

## Prepare the base for fresh instances

The original `bliss-base.qcow2` lacks the ADB startup fix. The existing default overlay already contains that repair.
New instances require **`vm/images/bliss-provisioned.qcow2`**. Creation reports a useful error until it exists.
The original base and default overlay are preserved. Do not replace a backing image used by existing overlays.

Run in a root Linux/WSL environment with `qemu-utils`, `attr`, `util-linux`, ext4 loop mounting, and the NBD kernel module:

```powershell
# LOCAL DEVELOPMENT only: authorize this PC's PUBLIC ADB key in the new base.
# Run from the repository root; no private key is copied.
$publicKey = (wsl wslpath -u "$env:USERPROFILE\.android\adbkey.pub").Trim()
wsl -u root -- sh image-build/prepare-instance-base.sh vm/images/bliss-base.qcow2 vm/images/bliss-provisioned.qcow2 $publicKey
```

Without the last argument the script provisions ADB startup without adding a host authorization key.
For distribution, prepare a clean base with no personal key, verify its Android RSA authorization UI,
and complete first-boot/setup testing. That distribution flow is not certified by local development tests.
The script refuses to overwrite existing output, modifies a temporary overlay only, unmounts both filesystems,
and publishes a detached qcow2 base after `qemu-img check`. Never run disk repair against an active instance.

On the first migration of `instances/default/overlay.qcow2`, the generated config uses the provisioned base
for future factory resets if it is already available; the existing overlay keeps its original backing chain.
If the default config was generated before preparation, review its `baseImage` before resetting.

## Manual acceptance checklist

- [ ] Existing default starts with its apps/data preserved.
- [ ] Instances toolbar button opens the launcher; `--instances` always opens it.
- [ ] Create two instances; each has `config.json` and a small `overlay.qcow2`.
- [ ] Invalid, duplicate, Windows reserved, and traversal names are rejected.
- [ ] Start both instances; launcher reports Running and their QMP/ADB ports differ.
- [x] Switch windows; native keyboard input affects only the focused guest (live typing and regression verified).
- [ ] A second DroidBox process cannot launch the same instance.
- [ ] Stop closes the selected VM gracefully; launcher changes to Stopped.
- [ ] Clone a stopped instance; an installed app or file appears in the clone.
- [ ] Running instance clone/reset/delete attempts report an error and preserve its disk.
- [ ] Settings show saved CPU, RAM, display, DPI, ports, and camera mode.
- [ ] CPU is limited to host cores (maximum 8); RAM is 1024 to half host RAM (maximum 8192), step 512.
- [ ] Duplicate ports are rejected across QMP and ADB, including externally occupied ports.
- [ ] Save settings, restart, and verify CPU/RAM process arguments and Android display values.
- [ ] Factory reset with confirmation clears instance data; Delete with confirmation removes only that instance.
- [ ] Canceling Delete/Reset preserves data.
- [ ] Camera None remains usable; unsupported test pattern is disabled; see [CAMERA.md](CAMERA.md).

## Hardware and guest commands

```powershell
powershell -ExecutionPolicy Bypass -File vm/scripts/check-whpx.ps1
.\vm\qemu\qemu-system-x86_64.exe -device help | Select-String 'usb-host|usb-video|camera'
.\vm\qemu\qemu-system-x86_64.exe -device virtio-vga-gl,help
Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" | Select-Object ProcessId,CommandLine
```

For each instance substitute the `adbHostPort` from its config:

```powershell
.\vm\adb\adb.exe -s localhost:5555 shell wm size
.\vm\adb\adb.exe -s localhost:5555 shell wm density
.\vm\adb\adb.exe -s localhost:5555 shell getprop sys.boot_completed
.\vm\adb\adb.exe -s localhost:5555 shell dumpsys media.camera
.\vm\adb\adb.exe -s localhost:5555 shell am start -W -a android.media.action.STILL_IMAGE_CAMERA
.\vm\adb\adb.exe -s localhost:5555 logcat -d -b crash
```

Do not repeatedly run `adb connect` if the guest is unavailable. Inspect guest boot/adbd diagnostics first.
