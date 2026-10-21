# DroidBox Test Harnesses

This directory contains test executables for each phase.

## Structure

| File | Phase | What It Tests |
|---|---|---|
| `test_vm_control.cpp` | Phase 1 | QEMU launch, QMP connect, pause/resume, and stopped-VM disk snapshots |
| `test_keymap.cpp` | Phase 3 | Keymap JSON parsing, action dispatch |
| `test_adb.cpp` | Phase 4 | ADB connect, install, push |
| `test_instance_manager.cpp` | Phase 5 | Instance CRUD, overlay creation |
| `CMakeLists.txt` | All | Build config for all tests |

## Running Tests

```powershell
# Build tests
cmake --build build --config Release --target test_vm_control

# Run a specific test
.\build\Release\test_vm_control.exe --config vm\configs\default-instance.json

# Allow the guest time to boot before the graceful-stop test (milliseconds)
.\build\Release\test_vm_control.exe --test graceful_stop `
  --config vm\configs\default-instance.json --boot-wait 60000

# Disk-only snapshot test: waits for boot, stops QEMU, saves/restores the qcow2
# overlay while QEMU is stopped, then starts QEMU again from the restored disk
.\build\Release\test_vm_control.exe --test snapshot `
  --config vm\configs\default-instance.json --boot-wait 60000

# Run keymap tests (no VM needed)
.\build\Release\test_keymap.exe
```

## Test Philosophy

- Tests are standalone console executables, not a test framework
- Each test prints `[PASS]` or `[FAIL]` with details
- VM-dependent tests require QEMU and a disk image
- Bliss OS 14 requires a Windows QEMU build with `virtio-vga-gl` and SDL OpenGL support; the controller enables these so Android can pass the text boot splash
- Phase 1 snapshots preserve qcow2 disk contents only; save and restore require a stopped VM, and RAM/device state is not captured
- Graceful stop requests guest ACPI shutdown first, then asks QEMU to exit if the guest does not respond; force termination is the last fallback
- Non-VM tests (keymap parsing, config loading) run anywhere
- Tests exit with code 0 (all pass) or 1 (any failure)
