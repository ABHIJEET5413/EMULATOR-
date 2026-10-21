# DroidBox Test Harnesses

This directory contains test executables for each phase.

## Structure

| File | Phase | What It Tests |
|---|---|---|
| `test_vm_control.cpp` | Phase 1 | QEMU launch, QMP connect, pause/resume, and stopped-VM disk snapshots |
| `test_display_input.cpp` | Phase 2 | QMP pointer-coordinate scaling and Qt-to-QMP keycode mapping |
| `test_keymap.cpp` | Phase 3 | Keymap JSON parsing, action dispatch |
| `test_adb.cpp` | Phase 4 | ADB connection/recovery, install, push, media scan, screenshot, navigation |
| `test_adb_live.cpp` | Phase 4 | Real guest connection, APK install, file push/media scan, screenshot, navigation |
| `test_instance_manager.cpp` | Phase 5 | Instance CRUD, overlay creation |
| `test_launch_channel.cpp` | Phase 6 | Single UI ownership and validated cross-process launch/APK forwarding |
| `test_whpx.ps1` | Phase 6 | WHPX script invocation and mocked status/enablement cases |
| `CMakeLists.txt` | All | Build config for all tests |

## Running Tests

```powershell
# Build tests
cmake --build build --config Release --target test_vm_control

# Run all non-hardware harness checks registered with CTest
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure

# Run a specific test
.\build\Release\test_vm_control.exe --config vm\configs\default-instance.json

# Run Phase 2 input translation checks (no VM or GUI required)
.\build\Release\test_display_input.exe

# Allow the guest time to boot before the graceful-stop test (milliseconds)
.\build\Release\test_vm_control.exe --test graceful_stop `
  --config vm\configs\default-instance.json --boot-wait 60000

# Disk-only snapshot test: waits for boot, stops QEMU, saves/restores the qcow2
# overlay while QEMU is stopped, then starts QEMU again from the restored disk
.\build\Release\test_vm_control.exe --test snapshot `
  --config vm\configs\default-instance.json --boot-wait 60000

# Run keymap tests (no VM needed)
.\build\Release\test_keymap.exe

# Run the complete Phase 4 ADB bridge harness (no running guest required)
.\build\Release\test_adb.exe

# Live Phase 4 checks: start DroidBox and wait for Ready first.
# This installs/reinstalls the APK you supply in that guest.
.\build\Release\test_adb_live.exe --adb vm/adb/adb.exe --apk C:/path/to/app.apk
```

The live harness is deliberately excluded from CTest because it requires a
running authorized guest and installs an APK. CTest passing alone does not
verify the Android image, toolbar dialogs, or native window drag and drop.

## Test Philosophy

- Tests are standalone console executables, not a test framework
- Each test prints `[PASS]` or `[FAIL]` with details
- VM-dependent tests require QEMU and a disk image
- Display embedding, actual guest input, toolbar, and fullscreen behavior require the manual GUI checklist in the Phase 2 guide
- Bliss OS 14 requires a Windows QEMU build with `virtio-vga-gl` and SDL OpenGL support; the controller enables these so Android can pass the text boot splash
- Phase 1 snapshots preserve qcow2 disk contents only; save and restore require a stopped VM, and RAM/device state is not captured
- Graceful stop requests guest ACPI shutdown first, then asks QEMU to exit if the guest does not respond; force termination is the last fallback
- Non-VM tests (keymap parsing, config loading) run anywhere
- Tests exit with code 0 (all pass) or 1 (any failure)

## Phase 5

`test_instance_manager.exe` creates temporary real qcow2 images with the bundled tools and checks CRUD,
clone data preservation, factory reset, safe names, cross-process-compatible locks, host limits, and port conflicts.
It is registered as `instance_manager` in CTest. Full build, hardware commands, and manual acceptance:
[Phase 5 testing](../docs/PHASE-5-TESTING.md).

`test_phase5_ui.exe` tests actual Qt settings and launcher controls with the offscreen platform plugin,
including all display presets, performance limits, port warnings, camera controls, and confirmed/canceled disk operations.
It is registered as `phase5_ui`; the Create-button case requires `vm/images/bliss-provisioned.qcow2`.
All disk mutations in this harness use isolated temporary instance directories.

`test_native_keyboard.exe` is registered as `native_keyboard` on Windows. It uses two foreign native
windows and QMP protocol fixtures to verify ordinary typing with keymaps disabled, focus isolation,
press/release codes, cross-window modifier release, and host dialog input. It requires an interactive
Windows desktop; it does not require Android or WHPX.

## Phase 6

`launch_channel` exercises the per-user UI lease, independent instance roots,
quoted/Unicode APK paths, malformed and oversized request rejection, and a
non-responsive owner. `whpx_script` checks the PowerShell script through its
real `-File` entry point and child-process mocks; it never enables Windows
features.

`test_installer.ps1` is a static packaging preflight registered as
`installer_preflight` on Windows. It parses packaging PowerShell scripts, checks
required license/assets/configuration, and optionally verifies every staged file
against `installer/bundle/manifest.json`. It does not install/uninstall anything
or change Windows features. Run manually with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_installer.ps1 -BundlePath "$PWD/installer/bundle"
```

Full installer/guest acceptance remains the [Phase 6 manual checklist](../docs/PHASE-6-TESTING.md).
