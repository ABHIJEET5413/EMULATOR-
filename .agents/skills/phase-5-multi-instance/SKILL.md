---
name: phase-5-multi-instance
description: >
  Implement Phase 5 of DroidBox: Multi-Instance Manager, Settings UI,
  and Camera support. Create/clone/delete instances, per-instance settings
  dialog, and evaluate webcam passthrough vs test-pattern camera.
---

# Phase 5: Multi-Instance Manager, Settings & Camera

## Overview

This phase adds the ability to run multiple Android instances, a settings
dialog for per-instance configuration, and camera support.

## Before You Start

1. Read `docs/phase-guides/phase-5-multi-instance.md` for the detailed guide.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Phase 4 must be complete and tested.

## What to Build

### Files to Create / Modify

| File | Purpose |
|---|---|
| `host-app/include/instance_manager.h` | InstanceManager class |
| `host-app/src/instance_manager.cpp` | Instance CRUD operations |
| `host-app/include/settings_dialog.h` | Settings UI dialog |
| `host-app/src/settings_dialog.cpp` | Settings dialog implementation |
| `host-app/include/instance_list_window.h` | Instance picker/launcher |
| `host-app/src/instance_list_window.cpp` | Instance list implementation |
| `host-app/src/main.cpp` | **Modify**: show instance list or launch |
| `docs/CAMERA.md` | Camera feasibility analysis |
| `tests/test_instance_manager.cpp` | Instance management tests |

---

### Part A: Multi-Instance Manager

#### How qcow2 Overlays Work
```
bliss-base.qcow2  (READ-ONLY, shared by all instances)
    ├── instances/default/overlay.qcow2   (copy-on-write)
    ├── instances/gaming/overlay.qcow2    (copy-on-write)
    └── instances/work/overlay.qcow2      (copy-on-write)

Each overlay only stores blocks that differ from the base.
Creating a new instance = creating a new overlay (instant, ~200KB).
Factory reset = delete overlay + create new one.
```

#### InstanceManager Class
```
Methods:
- listInstances() -> QList<InstanceInfo>
- createInstance(name, config) -> bool
  → Copy default config, create overlay with qemu-img
- cloneInstance(source, newName) -> bool
  → Copy overlay + config
- deleteInstance(name) -> bool
  → Delete overlay + config (confirm with user)
- factoryReset(name) -> bool
  → Delete overlay, create fresh one from base
- getConfig(name) -> InstanceConfig
- setConfig(name, config) -> bool
- getInstanceDir(name) -> QString
- isRunning(name) -> bool

Each instance lives in: instances/<name>/
  ├── config.json
  ├── overlay.qcow2
  └── snapshots/   (optional)
```

#### Creating an Overlay
```powershell
qemu-img create -f qcow2 -b ../../vm/images/bliss-base.qcow2 -F qcow2 overlay.qcow2
```
This creates a tiny overlay that references the base image.
All writes go to the overlay; reads fall through to the base.

#### Instance List Window
```
┌──────────────────────────────────────┐
│  DroidBox — Instances           _ X  │
├──────────────────────────────────────┤
│                                      │
│  ┌──────────┐  ┌──────────┐          │
│  │ 📱       │  │ 📱       │          │
│  │ Default  │  │ Gaming   │   [+]    │
│  │ Stopped  │  │ Running  │  Create  │
│  │          │  │          │   New    │
│  │ [Start]  │  │ [Stop]   │          │
│  │ [Config] │  │ [Config] │          │
│  │ [Clone]  │  │ [Clone]  │          │
│  │ [Delete] │  │ [Delete] │          │
│  │ [Reset]  │  │          │          │
│  └──────────┘  └──────────┘          │
│                                      │
└──────────────────────────────────────┘
```

---

### Part B: Settings Dialog

#### Per-Instance Settings
```json
{
  "name": "gaming",
  "cpuCores": 4,        // 1-8, limited by host
  "ramMB": 4096,         // 1024-8192, step 512
  "display": {
    "width": 1080,       // presets: 720x1280, 1080x1920, 1440x2560
    "height": 1920,
    "dpi": 240           // 160, 240, 320, 480
  },
  "network": {
    "adbHostPort": 5555  // must be unique per instance
  },
  "qmpPort": 4444,       // must be unique per instance
  "camera": {
    "mode": "none"       // "none", "test-pattern", "webcam"
  }
}
```

#### Settings Dialog Layout
```
┌─────────────────────────────────────────┐
│  Instance Settings: [name]              │
├─────────────────────────────────────────┤
│                                         │
│  Performance                            │
│  ┌─────────────────────────────────┐    │
│  │ CPU Cores:  [====4====] 1-8    │    │
│  │ RAM (MB):   [===4096===] slider │    │
│  └─────────────────────────────────┘    │
│                                         │
│  Display                                │
│  ┌─────────────────────────────────┐    │
│  │ Resolution: [1080x1920 ▼]      │    │
│  │ DPI:        [240 ▼]            │    │
│  └─────────────────────────────────┘    │
│                                         │
│  Camera                                 │
│  ┌─────────────────────────────────┐    │
│  │ Mode: (•) None                 │    │
│  │       ( ) Test Pattern         │    │
│  │       ( ) Webcam (experimental)│    │
│  └─────────────────────────────────┘    │
│                                         │
│  ┌─────────┐  ┌────────┐               │
│  │  Save   │  │ Cancel │               │
│  └─────────┘  └────────┘               │
│                                         │
│  ⚠ Changes take effect after restart   │
└─────────────────────────────────────────┘
```

Settings must be validated:
- CPU cores ≤ host core count
- RAM ≤ host RAM / 2 (leave room for Windows)
- Ports must not conflict with other running instances

---

### Part C: Camera Support

#### Research & Document Honestly

Create `docs/CAMERA.md` analyzing these approaches:

##### Approach 1: QEMU USB Passthrough (usb-host)
```
QEMU args: -device usb-host,vendorid=0x046d,productid=0x082d
- Passes a physical USB webcam directly to the guest
- Guest sees it as a native USB camera
- Problem: On Windows, QEMU's usb-host requires the WinUSB driver
  (installed via Zadig), which STEALS the device from Windows.
  The webcam won't work in Windows apps while QEMU has it.
- May not work with all webcams
- Requires user to install Zadig + select the right device

Verdict: Works in theory but painful UX on Windows. NOT recommended
as default.
```

##### Approach 2: Test Pattern / Virtual Camera
```
QEMU can emulate a USB camera that shows a test pattern or
feeds frames from a file/pipe.

Research needed:
- Does QEMU have a built-in virtual camera? (No built-in one)
- Can we create a v4l2 loopback equivalent on Windows? (No v4l2)
- Can we feed frames via a named pipe to a custom QEMU device? (Complex)

Simpler option: Use Android's built-in camera test mode or install
a camera app that works without hardware (e.g., Open Camera can
show a blank preview without crashing).

Verdict: Document what works and what doesn't. Be honest.
```

##### Approach 3: QEMU -device usb-video with host camera via ffmpeg
```
Some QEMU builds support capturing from a host camera and presenting
it as a USB video device to the guest.

Research: Check QEMU docs for -device usb-video or similar options
on Windows. This is platform-specific and may require custom QEMU
patches.

Verdict: Research and document findings.
```

**Implementation plan:**
1. Try to make the Camera app not crash (may need a dummy camera device)
2. Document what works
3. If webcam passthrough works easily, include it as "experimental"
4. Default to "none" or "test-pattern" in the config

---

## Port Allocation for Multi-Instance

Each instance needs unique ports:
```
Instance 0: QMP=4444, ADB=5555
Instance 1: QMP=4445, ADB=5556
Instance 2: QMP=4446, ADB=5557
...
Formula: basePort + instanceIndex
```

The InstanceManager must ensure no port conflicts.

## Manual Test Checklist

### Multi-Instance
- [ ] Can create a new instance
- [ ] New instance gets its own overlay (check file exists)
- [ ] New instance starts with correct ports
- [ ] Can run two instances simultaneously (if enough RAM)
- [ ] Can clone an instance (data is preserved in clone)
- [ ] Can delete an instance (files are removed)
- [ ] Factory reset creates a fresh overlay
- [ ] Instance list shows correct running/stopped status

### Settings
- [ ] Settings dialog opens and shows current values
- [ ] Changing CPU cores and saving works (check config.json)
- [ ] Changing RAM and saving works
- [ ] Changing resolution and saving works
- [ ] Port conflict detection works (shows error if duplicate)
- [ ] Settings take effect after restarting the instance

### Camera
- [ ] Camera app launches without crashing (even if black screen)
- [ ] `docs/CAMERA.md` honestly documents what works and what doesn't
- [ ] If webcam passthrough works: documented as experimental with steps
- [ ] If it doesn't work: documented with explanation
