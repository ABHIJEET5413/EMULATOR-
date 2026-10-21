# DroidBox Architecture

> This document describes every module in the DroidBox Android emulator,
> what it does, and why it exists. Updated as each phase is built.

---

## System Overview

```
┌─────────────────────────────────────────────────────────┐
│                   DroidBox Host App                     │
│              (C++ / Qt 6 Desktop Application)           │
│                                                         │
│  ┌─────────┐  ┌──────────┐  ┌────────┐  ┌───────────┐  │
│  │ Display  │  │  Input   │  │  ADB   │  │ Instance  │  │
│  │ Embed    │  │ Handler  │  │ Bridge │  │ Manager   │  │
│  └────┬─────┘  └────┬─────┘  └───┬────┘  └─────┬─────┘  │
│       │             │            │              │        │
│  ┌────┴─────────────┴────────────┴──────────────┴─────┐  │
│  │              QemuController (QMP socket)            │  │
│  └────────────────────────┬───────────────────────────┘  │
└───────────────────────────┼──────────────────────────────┘
                            │
              ┌─────────────▼──────────────┐
              │          QEMU 8.x          │
              │   -accel whpx  (Hyper-V)   │
              │   -device usb-tablet       │
              │   -netdev user,hostfwd     │
              │   -qmp tcp:localhost:4444   │
              │   virtio-vga-gl + SDL/OpenGL│
              ├────────────────────────────┤
              │    Bliss OS (Android-x86)  │
              │    qcow2 overlay on base   │
              │    ADB over TCP :5555      │
              └────────────────────────────┘
```

---

## Module Descriptions

### 1. `host-app/` — Qt 6 Desktop Application

The main Windows application the user interacts with.

#### Entry point and logging (Phase 1)
- `src/main.cpp` creates the headless `QCoreApplication` entry point.
- A Qt message handler writes timestamped messages to `droidbox.log`
  under the user's application-local data directory and to stderr.
- The Phase 1 CLI test harness is the way to exercise VM controls; the
  desktop GUI is introduced in Phase 2.

#### 1.1 `QemuController` (Phase 1)
- **What**: Manages the QEMU process lifecycle (start, stop, kill).
- **Why**: We need programmatic control over the VM without asking the
  user to run QEMU manually.
- **How**: Loads the instance JSON, resolves QEMU and overlay paths,
  launches `qemu-system-x86_64.exe` via `QProcess` with WHPX, and waits
  up to 30 seconds for the QMP handshake. It builds the Phase 1 command
  line with the configured CPU/RAM, qcow2 overlay, USB tablet, user-mode
  NAT/ADB forward, OpenGL virtio VGA with an SDL display, QMP socket, and
  serial console. This graphics device lets Bliss 14 pass the text boot
  splash on the tested Windows host. Graceful
  shutdown first requests `system_powerdown`; if the guest does not respond,
  it asks QEMU to exit through QMP `quit`, and force-terminates only if QEMU
  still does not exit. Disk-only qcow2 snapshots use the bundled
  `qemu-img.exe` while QEMU is stopped; loading one restores disk contents
  and requires the guest to boot again, without RAM/device state.
- **Key classes**: `QemuController`, `QmpClient`
- **Operations**: `start`, `stop`, `kill`, `pause`, `resume`,
  `queryStatus`, `saveSnapshot`, and `loadSnapshot`.

#### 1.2 `QmpClient` (Phase 1)
- **What**: Low-level QMP (QEMU Machine Protocol) socket client.
- **Why**: QMP is a JSON-based protocol over TCP; we need a reliable
  async client that handles the handshake, sends commands, and parses
  responses.
- **How**: Uses `QTcpSocket` (Qt Network), buffers partial reads, and
  splits newline-delimited JSON messages. It reads QEMU's greeting,
  sends `qmp_capabilities`, assigns IDs to commands, routes responses
  and asynchronous events, and retries a lost connection every 500 ms
  while the controller still owns a running QEMU process.

#### 1.3 `MainWindow` (Phase 2)
- **What**: Qt Widgets desktop window with the guest display, side toolbar,
  and VM status bar.
- **Why**: Presents the Phase 1 QEMU controller as one interactive desktop app.
- **How**: Starts the default instance, connects controller lifecycle signals,
  shows user-facing errors, logs placeholder toolbar and drag/drop actions,
  and requests VM shutdown before closing. Fullscreen toggles immediately.

#### 1.4 `DisplayEmbed` (Phase 2)
- **What**: Embeds the QEMU guest display inside the Qt main window.
- **Why**: The emulator must feel like one application, not QEMU in a
  separate window.
- **How**: The current implementation uses Win32 `SetParent()` to find
  QEMU's visible SDL window by the child process ID, remove its top-level
  decorations, and resize it to the Qt display widget. The existing SDL /
  OpenGL rendering path stays active. VNC remains a fallback if reparenting
  proves unstable on a target Windows setup.

#### 1.5 `InputHandler` (Phase 2)
- **What**: Translates host mouse/keyboard events to guest input.
- **Why**: User expects to use their mouse as a finger and their
  keyboard for typing inside Android.
- **How**: With the embedded SDL child focused, Windows routes mouse and
  keyboard events directly to QEMU, which passes absolute tablet input via
  `usb-tablet` and keyboard input to Android. The Qt event filter also
  forwards mouse and supported key events through QMP when events are
  delivered to the Qt display widget instead of the native QEMU child.

#### 1.6 `KeymapEngine` (Phase 3)
- **What**: Maps physical keyboard keys to screen tap/swipe positions.
- **Why**: Gamers want WASD→d-pad, etc.
- **How**: JSON profiles (`keymaps/*.json`) define key→action mappings.
  Actions can be: tap at (x,y), swipe from (x1,y1) to (x2,y2), or
  key passthrough.

#### 1.7 `AdbBridge` (Phase 4)
- **What**: Manages ADB connection to the guest Android.
- **Why**: ADB is required for APK install, file push, screenshots,
  and running shell commands inside the guest.
- **How**: Runs `adb.exe` commands via `QProcess`. Connects to the
  guest via `adb connect localhost:<port>` (port forwarded by QEMU's
  `-netdev user,hostfwd=tcp::5555-:5555`).

#### 1.8 `InstanceManager` (Phase 5)
- **What**: Manages multiple Android instances.
- **Why**: Users may want multiple instances (different apps, accounts).
- **How**: Each instance = one JSON config + one qcow2 overlay.
  The base image is read-only; `qemu-img create -f qcow2 -b base.qcow2`
  creates a copy-on-write overlay per instance. Factory reset =
  delete the overlay and create a new one.

#### 1.9 `SettingsManager` (Phase 5)
- **What**: Per-instance settings (CPU cores, RAM, resolution, DPI).
- **Why**: Different apps need different resources.
- **How**: JSON file per instance in `instances/<name>/config.json`.
  Settings are translated to QEMU command-line arguments.

---

### 2. `vm/` — VM Configuration & Scripts

#### 2.1 `configs/`
- Default instance config JSON (template).
- QEMU command-line argument templates.

#### 2.2 `scripts/`
- `check-whpx.ps1`: Detects if VT-x is enabled and if Windows
  Hypervisor Platform is installed.
- `enable-whpx.ps1`: Enables the WHP Windows feature (requires admin).

---

### 3. `image-build/` — Disk Image Pipeline

- **What**: Scripts/docs to produce a pre-installed Bliss OS qcow2.
- **Why**: Users must never see the Android installer or setup wizard.
- **How**: 
  1. Download Bliss OS ISO.
  2. Create a raw/qcow2 disk.
  3. Boot QEMU from ISO, install to disk (automated or manual).
  4. Mount the installed image, apply config overlays:
     - Skip setup wizard (`provision.prop`).
     - Enable ADB over TCP on port 5555.
     - Pre-install F-Droid APK.
     - Set default resolution/DPI.
  5. Convert to qcow2 with compression.
  6. This becomes the read-only base image.

---

### 4. `installer/` — Inno Setup Packaging

- **What**: Inno Setup script that produces `DroidBoxSetup.exe`.
- **Why**: Single offline installer, no internet needed.
- **Bundles**:
  - `DroidBox.exe` + Qt DLLs (dynamic)
  - `qemu-system-x86_64.exe` + QEMU DLLs
  - `bliss-base.qcow2` (compressed base image)
  - `adb.exe` + platform-tools
  - MSVC runtime redistributable
  - License files + source offers
- **Creates**: Start Menu shortcut, Desktop shortcut, `.apk`
  association, uninstaller.
- **Startup check**: Runs WHPX detection, offers to enable if missing.

---

### 5. `tests/` — Test Harnesses

- `test_vm_control.cpp` (Phase 1): CLI harness for config loading,
  WHPX detection, QEMU launch, QMP connection/status, pause/resume,
  stopped-VM disk snapshot save/restore, graceful shutdown, and force
  termination. It
  accepts `--config`, `--qemu-path`, `--test`, and `--timeout` options.
- `CMakeLists.txt`: Builds the Phase 1 harness against `droidbox_core`.
- Later phases add their own standalone harnesses for keymaps, ADB,
  and instance management.

---

## Data Flow Diagrams

### APK Installation Flow
```
User drags .apk onto window
        │
        ▼
Qt drop event → AdbBridge::installApk(path)
        │
        ▼
QProcess: adb install -r <path>
        │
        ▼
Parse stdout → success/failure
        │
        ▼
Show toast notification in host UI
```

### VM Lifecycle
```
Phase 1 CLI harness calls QemuController::start(config)
        |
        +-- Load JSON and resolve runtime paths
        +-- Launch QEMU with WHPX through QProcess
        +-- Retry the QMP TCP connection
        +-- QmpClient reads greeting and sends qmp_capabilities
                |
                v
        CLI queries status and pauses/resumes over QMP
                |
                v
        QemuController sends system_powerdown
          then QMP quit on timeout, force-terminating only if needed
                |
                v
        With QEMU stopped, qemu-img saves/applies qcow2 disk snapshots;
        the restored disk is booted again

Phase 2+ connects the same controller to the GUI, display, input,
and ADB modules.
```

---

## Design Decisions Log

| Decision | Chosen | Why |
|---|---|---|
| Hypervisor | WHPX (Hyper-V) | HAXM is deprecated; WHPX works on Win10/11 with Hyper-V |
| Display backend | Win32 SetParent with SDL/OpenGL | Reuses the existing QEMU display and avoids framebuffer copies; VNC is the fallback if reparenting is unstable |
| Input method | Native SDL input plus QMP event forwarding | Native child-window input is direct; QMP handles events delivered to the Qt display widget |
| Disk format | qcow2 overlay | Copy-on-write enables multi-instance + factory reset |
| App store | F-Droid | Legal alternative to Play Store |
| Installer | Inno Setup | Free, mature, widely trusted on Windows |
| Build system | CMake | Cross-IDE, Qt 6 native support |
| Qt linking | Dynamic (LGPL) | Comply with Qt LGPL by dynamic linking |
