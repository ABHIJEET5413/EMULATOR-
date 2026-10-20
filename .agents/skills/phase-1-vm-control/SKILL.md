---
name: phase-1-vm-control
description: >
  Implement Phase 1 of DroidBox: VM Control. Launch/stop QEMU with WHPX
  acceleration, QMP connection, pause/resume/snapshot, boot Bliss OS from
  the pre-built qcow2 image. Includes a CLI test harness.
---

# Phase 1: VM Control

## Overview

This phase builds the foundation: launching QEMU as a child process,
connecting to it via QMP (QEMU Machine Protocol), and providing
pause/resume/snapshot/shutdown controls. No GUI yet — just a CLI test
harness to prove everything works.

## Before You Start

1. Read `docs/phase-guides/phase-1-vm-control.md` for the detailed
   implementation guide with class-by-class breakdown.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Read `docs/ARCHITECTURE.md` for the system overview.

## What to Build

### Files to Create

| File | Purpose |
|---|---|
| `host-app/CMakeLists.txt` | CMake build configuration |
| `host-app/src/main.cpp` | Entry point with file logging setup |
| `host-app/include/qemu_controller.h` | QemuController class declaration |
| `host-app/src/qemu_controller.cpp` | QemuController implementation |
| `host-app/include/qmp_client.h` | QmpClient class declaration |
| `host-app/src/qmp_client.cpp` | QMP socket protocol implementation |
| `vm/configs/default-instance.json` | Default VM configuration template |
| `vm/scripts/check-whpx.ps1` | WHPX detection script |
| `tests/test_vm_control.cpp` | CLI test harness |
| `tests/CMakeLists.txt` | Test build configuration |

### Classes to Implement

#### `QmpClient` (lowest level)
```
Responsibilities:
- Connect to QMP TCP socket (localhost:4444)
- Perform QMP handshake (read greeting, send qmp_capabilities)
- Send JSON-RPC commands and receive responses
- Handle async events from QEMU
- Reconnect on disconnect

Signals:
- connected()
- disconnected()
- responseReceived(QString id, QJsonObject result)
- eventReceived(QString event, QJsonObject data)
- errorOccurred(QString message)

Key methods:
- connectToHost(host, port)
- execute(command, arguments) -> returns command ID
- isConnected()
```

#### `QemuController` (higher level, uses QmpClient)
```
Responsibilities:
- Build QEMU command line from instance config JSON
- Launch qemu-system-x86_64.exe via QProcess
- Wait for QMP socket to become available
- Provide high-level VM operations

Methods:
- start(configPath) -> bool
- stop()           -> graceful shutdown via QMP
- kill()           -> force kill the process
- pause()          -> QMP "stop"
- resume()         -> QMP "cont"
- saveSnapshot(name) -> qemu-img qcow2 disk snapshot; QEMU must be stopped
- loadSnapshot(name) -> restore qcow2 disk snapshot; QEMU must be stopped
- getStatus()      -> running/paused/stopped

Signals:
- vmStarted()
- vmStopped(exitCode)
- vmError(QString message)
- qmpConnected()
```

### QEMU Command Line

The controller must build this command line from the instance config:

```
qemu-system-x86_64.exe
  -accel whpx,kernel-irqchip=off
  -m 4096
  -smp 1
  -cpu max
  -drive file=instances/default/overlay.qcow2,format=qcow2,if=virtio
  -device usb-ehci
  -device usb-tablet
  -netdev user,id=net0,hostfwd=tcp::5555-:5555
  -device virtio-net-pci,netdev=net0
  -display sdl
  -qmp tcp:localhost:4444,server,wait=off
  -serial mon:stdio
  -boot c
```

### Instance Config JSON Schema

```json
{
  "name": "default",
  "cpuCores": 1,
  "ramMB": 4096,
  "display": {
    "width": 1080,
    "height": 1920,
    "dpi": 240
  },
  "qemuPath": "vm/qemu/qemu-system-x86_64.exe",
  "baseImage": "vm/images/bliss-base.qcow2",
  "overlayImage": "instances/default/overlay.qcow2",
  "qmpPort": 4444,
  "adbHostPort": 5555
}
```

### WHPX Check Script

`vm/scripts/check-whpx.ps1` must:
1. Check if CPU virtualization is enabled (WMI query)
2. Check if Windows Hypervisor Platform feature is enabled
3. Output clear pass/fail messages
4. Exit with code 0 (all good) or 1 (problem found)

### CLI Test Harness

`tests/test_vm_control.cpp` — a console application that:
1. Parses command-line args (--config, --qemu-path)
2. Runs the WHPX check
3. Creates a QemuController
4. Starts the VM
5. Waits for QMP connection
6. Runs: query-status, pause, resume, and graceful shutdown
7. Saves and applies a disk-only qcow2 snapshot while QEMU is stopped
8. Starts QEMU again from the restored disk, then tests force termination
8. Reports pass/fail for each step

## Build Instructions to Provide

```powershell
# Configure
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.5.3/msvc2019_64"

# Build
cmake --build build --config Release

# Run WHPX check
powershell -ExecutionPolicy Bypass -File vm/scripts/check-whpx.ps1

# Run test harness
.\build\Release\test_vm_control.exe --config vm/configs/default-instance.json
```

## Manual Test Checklist

After building, the user should verify:

- [ ] `check-whpx.ps1` reports virtualization status correctly
- [ ] QEMU launches and a window appears showing BIOS/boot
- [ ] QMP connects (test harness prints "QMP connected")
- [ ] `query-status` returns valid JSON
- [ ] Pause command works (QEMU window freezes)
- [ ] Resume command works (QEMU window unfreezes)
- [ ] Disk-only snapshot save completes after QEMU stops
- [ ] Disk-only snapshot restore completes and the guest boots again
- [ ] Graceful stop works (ACPI request or QMP quit exits QEMU without force termination)
- [ ] Force kill works as fallback
- [ ] Log file is created with readable messages
- [ ] If WHPX is unavailable, error message explains how to fix it

## What NOT to Build Yet

- No Qt GUI (that's Phase 2+)
- No display embedding (Phase 2)
- No ADB connection (Phase 4)
- No multi-instance (Phase 5)
- The test harness IS the UI for this phase

## Notes for the Implementer

- You will need QEMU for Windows. The user must download it separately
  for development (the installer bundles it later in Phase 6).
- If the user doesn't have a Bliss OS qcow2 yet, the test can use any
  bootable ISO/qcow2 — document this clearly.
- The `kernel-irqchip=off` flag is needed for WHPX on some systems.
  If boot fails, try without it and document the behavior.
- QMP uses a newline-delimited JSON protocol. Each message is one line
  of JSON terminated by `\r\n`. The greeting is sent by QEMU, then you
  send `{"execute": "qmp_capabilities"}`, then you can send commands.
