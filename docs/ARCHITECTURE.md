# Sabi Architecture

> This document describes every module in the Sabi Android emulator,
> what it does, and why it exists. Updated as each phase is built.

---

## System Overview

```
┌─────────────────────────────────────────────────────────┐
│                     Sabi Host App                       │
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
              │     QEMU 11.1.0 (ar7)     │
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
- `src/main.cpp` creates the Qt Widgets application and instance launcher.
  Installer initialization uses `QCoreApplication` with `--initialize-only`.
- `LaunchChannel` (`launch_channel.h/.cpp`) grants one UI process a lock per
  instance data root and forwards later APK/instance launch requests over a
  user-only local socket. Requests are size-limited and validated; a second
  window is not started when the owner is still initializing.
- A Qt message handler writes timestamped messages to `sabi-<pid>.log`
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
- **How**: Starts the default instance and connects controller lifecycle
  signals. When QEMU starts, it asks `AdbBridge` to connect to the configured
  ADB host port on `localhost`; the status bar follows ADB connection and Android boot
  progress. Home, Back, Recents, volume, and Power actions send Android key
  events. Screenshot and APK buttons open file dialogs. Dropped APKs are
  installed, other dropped files are copied to `/sdcard/Download/`, and an
  APK path passed on the command line is installed after Android is ready.
  It opens 900 pixels wide by default so Android's first-run approval dialogs
  fit beside the vertical toolbar. It also shows user-facing errors and
  requests VM shutdown before closing.
  Fullscreen toggles immediately.

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
- **How**: Windows routes native mouse events to the embedded SDL child.
  While that child owns focus, a low-level keyboard hook sends mapped actions
  and ordinary keyboard press/release events through its controller's QMP
  connection. Ordinary typing works with keymaps disabled and does not depend
  on SDL receiving keyboard messages after reparenting. Successfully forwarded
  keys are consumed to avoid duplicates. Key-up reaches every handler owning
  the key, including after switching guests; host dialogs retain normal typing.
  The Qt event filter handles keys delivered to the Qt display widget, and mouse
  events use QMP when delivered there. Touch coordinates are sent as QMP
  absolute axes and button events.

#### 1.6 `KeymapEngine` (Phase 3)
- **What**: Maps physical keyboard keys to guest touch actions.
- **Why**: Games and other apps can use keyboard keys for touch controls.
- **How**: `KeymapProfile` reads the documented JSON schema from
  `resources/keymaps/*.json`. `KeymapEngine` scales profile coordinates to
  the configured guest resolution and dispatches tap, hold, timed swipe,
  repeat-tap, or passthrough actions. `InputHandler` routes key press and
  release events through the engine first, then sends touch and passthrough
  events with `QemuController::sendInputEvents()`. Hold and repeat timers
  are stopped and released when keys are released or focus is lost. The
  default and WASD gaming profiles are staged beside the executable.

#### 1.7 `KeymapProfile` (Phase 3)
- **What**: Data model for named key bindings, profile resolution, and
  mouse-map metadata.
- **Why**: Profiles can be loaded, validated, saved, and changed at runtime
  without changing the input dispatch code.
- **How**: JSON parsing rejects malformed documents and invalid action
  parameters; missing optional fields use the guide's defaults. Key names
  are converted to Qt keys, and profile coordinates are scaled before they
  are normalized to QMP's absolute input range.
  Numeric keyboard events use QMP's XT set-1 values, including extended
  navigation keys; they are not Linux input event keycodes.

#### 1.8 `AdbBridge` (Phase 4)
- **What**: Manages the host ADB process and guest Android connection state.
- **Why**: ADB is required for APK install, file push, screenshots,
  navigation controls, media indexing, and Android shell commands.
- **How**: Locates `adb.exe` beside the application in `vm/adb` or `adb`,
  then falls back to `PATH`. Each operation runs asynchronously in its own
  `QProcess`. On VM start it starts the shared ADB server without killing
  other applications' ADB sessions,
  reads the forwarded host port from the QEMU instance configuration, probes
  for a real ADB CNXN/AUTH packet before invoking `adb connect`, and verifies
  `adb get-state` reports
  `device` before marking the guest connected. Offline
  transports stop automatic retries and report guest network/listener
  checks. Unauthorized transports emit `authorizationRequired`, show guest
  approval instructions in the status bar, and poll only the existing
  transport's `get-state` for up to three minutes. They do not repeat
  `adb connect` or disconnect while Android displays its RSA prompt. Approval
  continues into the boot/Ready flow without restarting Sabi.
  An open host forward alone does not prove that
  Android's `adbd` is running. The provisioned guest uses
  `image-build/droidbox-adb.rc` to start authenticated TCP ADB on port 5555
  when Android finishes booting. It then polls
  `shell getprop sys.boot_completed` until Android is ready. A health poll
  detects disconnects and reconnects. APK installation uses `adb install -r`;
  file transfer uses `adb push` followed by Android's media-scanner broadcast;
  screenshots use `exec-out screencap -p`, validate PNG decoding, and are
  saved atomically. Callbacks from an earlier connection generation are
  discarded after disconnect or guest replacement.
  Guest commands target `localhost:<adbHostPort>`. The QEMU user-mode forward
  maps that configured host TCP port to guest TCP port 5555.

  `image-build/droidbox-first-boot.sh` runs as Android's shell UID before the
  explicit TCP daemon start. It enables the framework's ADB debugging manager
  so RSA authorization works without pre-installed host keys, retaining
  `ro.adb.secure=1`. On the first boot of each writable overlay it selects Bliss
  Launcher and records completed setup; a marker prevents later boots from
  replacing the user's launcher choice. `provision-adb-image.sh` installs and
  labels both startup files in a new provisioned image.

#### 1.9 `InstanceManager` (Phase 5)
- **What**: Manages multiple Android instances.
- **Why**: Users may want multiple instances (different apps, accounts).
- **How**: Each instance = one JSON config + one qcow2 overlay.
  The base image is read-only; `qemu-img create -f qcow2 -b base.qcow2`
  creates a copy-on-write overlay per instance. Factory reset stages and
  validates a replacement before replacing the stopped instance's overlay.

#### 1.10 `SettingsDialog` (Phase 5)
- **What**: Per-instance settings (CPU cores, RAM, resolution, DPI).
- **Why**: Different apps need different resources.
- **How**: Edits are validated and saved through `InstanceManager` to
  `instances/<name>/config.json`. CPU/RAM/display settings apply on restart;
  Android resolution and DPI are also applied through ADB after boot.

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
     - Pre-install the Neo Store F-Droid client.
     - Set default resolution/DPI.
  5. Convert to qcow2 with compression.
  6. This becomes the read-only base image.

---

### 4. `installer/` — Inno Setup Packaging

- **What**: Inno Setup script that produces `SabiSetup.exe`.
- **Why**: Single offline installer, no internet needed.
- **Bundles**:
  - `Sabi.exe` + Qt DLLs (dynamic)
  - `qemu-system-x86_64.exe` + QEMU DLLs
  - `bliss-provisioned.qcow2` (clean, standalone distribution base)
  - `adb.exe` + platform-tools
  - MSVC runtime redistributable
  - License files + source offers
- **Creates**: Start Menu shortcut, Desktop shortcut, `.apk`
  association, uninstaller.
- **Startup check**: Runs WHPX detection, offers to enable if missing.

#### Phase 6 implementation

- `sabi.iss`: one solid-compressed x64 offline EXE, full license screen,
  shortcuts, quoted APK invocation and Open With registration. Its optional
  machine association preserves the previous default; Windows UserChoice is not
  overwritten. Runtime installation and original-user initialization are checked
  for actual success. Reboot or acceleration failures suppress automatic launch.
- `check_whpx.ps1`: firmware/hypervisor and Windows feature checks, optional
  offline DISM enablement, explicit exit codes/JSON, and a diskless QEMU WHPX
  startup probe. Errors are written to the installer log. No forced reboot.
- `scripts/post-install.ps1`: invoked by `ExecAsOriginalUser`; runs the host's
  `--initialize-only` mode to use the same atomic configuration, locking and port
  allocation as the GUI, without starting Android.
- Installed layout is marked by `installed-layout.json`, containing the reviewed
  base SHA-256/size. The executable directory takes precedence over arbitrary
  working directories. A new user atomically copies and hashes the base into
  `AppLocalDataLocation/bases/<SHA-256>/bliss-provisioned.qcow2`; all new instance
  overlays use that retained base. Older versions are preserved for existing
  disks. Existing instances need no new base copy until a new instance is made.
  Installed configurations resolve the current bundled QEMU path on reading, so
  preserved data also works after a reinstall to another application directory.
- `scripts/uninstall-data.ps1`: optional current-account cleanup, with fixed
  path validation, reparse-point refusal and disk writer checks. No/silent
  uninstall keeps both overlays and backing images. Other accounts are retained.
- `scripts/prepare-bundle.ps1`: verifies pinned inputs, deploys dynamic Qt/plugins,
  stages QEMU/ADB/runtime/firmware/keymaps and creates a per-file hash manifest.
  Installer inputs/output are ignored by Git; only packaging sources are tracked.
- `source-lock.json`, `licenses/`, and bundled `sources/`: full requested license
  texts, exact core source archives, local changes and source-request offer.
  `build-installer.ps1` verifies staged hashes and invokes Inno Setup. Public
  distribution additionally requires a reviewed complete source inventory; the
  default development installer does not pretend that review has happened.
- `tests/test_installer.ps1`: static packaging and optional payload-hash preflight;
  it does not replace clean-machine install/uninstall or guest boot acceptance.

Build commands and installed layout: [installer/BUILD.md](../installer/BUILD.md).
Manual acceptance: [PHASE-6-TESTING.md](PHASE-6-TESTING.md).

---

### 5. `tests/` — Test Harnesses

- `test_vm_control.cpp` (Phase 1): CLI harness for config loading,
  WHPX detection, QEMU launch, QMP connection/status, pause/resume,
  stopped-VM disk snapshot save/restore, graceful shutdown, and force
  termination. It
  accepts `--config`, `--qemu-path`, `--test`, and `--timeout` options.
- `CMakeLists.txt`: Builds the Phase 1 harness against `droidbox_core`.
- `test_display_input.cpp` (Phase 2): Checks display-coordinate scaling
  and Qt-to-QMP key-code conversion.
- `test_keymap.cpp` (Phase 3): Checks profile JSON parsing/saving, key
  lookup, all five key actions, profile-to-guest coordinate scaling, and
  profile enumeration.
- `test_adb.cpp` (Phase 4): Runs the ADB bridge against a fake ADB executable
  and checks connection and boot polling, APK install, file push and media
  scan, PNG screenshot saving, every navigation key event, offline-transport
  recovery, disconnect handling, and a readable missing-ADB error. It does
  not require a running VM or the Android SDK platform-tools. All four phase
  harnesses are registered with CTest; its Phase 1 entry checks configuration
  loading, while the VM lifecycle variants remain explicit hardware tests.
- `test_adb_live.cpp` (Phase 4): Explicit real-guest checks for authenticated
  connection, APK install, file push/media scan, screenshot, and navigation.
  Not registered with CTest because it installs an APK into a running guest.
- Later phases add standalone harnesses for instance management.

---

## Data Flow Diagrams

### APK Installation Flow
```
User selects, drops, or opens an .apk
        |
        v
MainWindow queues it until Android reports boot complete
        |
        v
AdbBridge -> QProcess: adb -s localhost:<adbHostPort> install -r <path>
        |
        v
Show success in the status bar or the ADB error in a dialog
```

### ADB Connection and File Flow
```
QemuController::vmStarted -> AdbBridge::connectToGuest(localhost, adbHostPort)
        |
        +-- adb start-server
        +-- wait for guest CNXN/AUTH protocol response, then adb connect
        +-- verify adb get-state reports device; stop on offline/unauthorized transports
        +-- poll sys.boot_completed and report Ready
        +-- poll device health and reconnect after a disconnect

Dropped non-APK file -> adb push -> /sdcard/Download/<filename>
                                      -> media scanner broadcast
Screenshot action -> adb exec-out screencap -p -> atomic local PNG save
Navigation toolbar -> adb shell input keyevent <Android keycode>
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
```

Phase 2+ connects the same controller to the GUI, display, input,
and ADB modules.

Phase 3 routes a key event through the active keymap profile first:

```
Physical key → InputHandler → KeymapEngine
                    ├─ mapped action → scaled touch event → QMP
                    ├─ passthrough binding → QMP key event
                    └─ unmapped key → existing keyboard path
```

---

## Phase 5 modules

- `InstanceManager` (`instance_manager.h/.cpp`): per-instance JSON and qcow2 lifecycle,
  safe names/directories, atomic settings, real host CPU/RAM limits, unique ports across both roles,
  external port occupancy checks, registry-operation lock, and a cross-process running lock.
  A Windows handle denying write sharing protects disk operations against unmanaged QEMU/disk-tool writers;
  a successful read-only qemu-img check alone is not treated as an exclusive lock.
  Clone reads the real backing chain, copies a stopped overlay including internal snapshots,
  and normalizes the copied backing filename while sharing the same backing image.
  Reset stages a fresh disk and retains the original until replacement succeeds.
- `InstanceListWindow` (`instance_list_window.h/.cpp`): launcher with create/start/stop/settings/clone/delete/reset,
  running-state refresh, confirmation before erasure, and disk work on a dedicated QThread.
  Each managed `MainWindow` has its own controller, ADB bridge, display, and input handler.
- `SettingsDialog` (`settings_dialog.h/.cpp`): CPU/RAM sliders/spinboxes, per-instance Phone, Tablet,
  PC, Fullscreen, and Custom display profiles, DPI, QMP/ADB ports, and optional USB VID/PID. Profile
  dimensions/DPI are stored in the selected instance's `config.json`, restored on the next launch, and
  applied through ADB when Android is ready. CPU/RAM and QEMU raster changes still require restart.
- `MainWindow` exposes the main display profiles in the toolbar and has a Rotate action. It saves profile
  changes per instance, applies `wm size`/`wm density` live after boot, and saves rotation as Custom.
- The Sabi host uses a compact dark icon toolbar with accessible action tooltips. Shared Folder is deliberately
  hidden because the guest does not yet provide a verified file-sharing transport; file drag-and-drop remains
  the supported ADB transfer path.
- `main.cpp`: imports the existing default disk without recreating it, launches the sole instance directly,
  otherwise shows the launcher. APK arguments target default; `--instance NAME` selects one;
  `--instances` explicitly opens the launcher. Installed writable data uses AppLocalDataLocation.
  File logs use `droidbox-<pid>.log` to preserve complete records across multiple app processes.
- `MainWindow` now wires Settings and Instances actions. It sizes the initial host window for the selected
  guest aspect ratio within the active monitor's work area. Boot completion applies numeric `wm size`/`wm density`
  settings through that instance's ADB serial and updates keymap coordinate scaling.
- `DisplayEmbed` keeps the SDL child in a centered, aspect-correct rectangle instead of stretching it across
  letterbox space. The same physical-pixel rectangle is given to Win32. `InputHandler` intercepts mouse input
  over that embedded child and sends it through QMP's absolute USB-tablet path using the actual Win32 client
  rectangle, avoiding SDL reparenting/scaling cursor displacement.
- `QemuController` applies display width/height as virtio-vga-gl xres/yres and validates optional usb-host IDs.
  The instance running lock remains held until QEMU has exited.
- `image-build/prepare-instance-base.sh`: creates a new provisioned base via a disposable overlay;
  the original immutable base and existing default data are never mounted for writing.
  A locally authorized development base is kept out of Git and must not be distributed.
- `test_instance_manager.cpp`: real qcow2 CRUD/data preservation, reset, validation, port occupancy,
  duplicate launch and running-disk protection. See [Phase 5 checks](PHASE-5-TESTING.md) and [camera findings](CAMERA.md).
- `test_phase5_ui.cpp`: offscreen tests for settings controls, Save/Cancel, conflict warnings,
  launcher disk-operation buttons, and confirmation dialogs using isolated temporary instances.
- `test_native_keyboard.cpp`: Windows desktop regression using two foreign native process windows
  and QMP protocol fixtures. Checks typing with keymaps disabled, focused-guest isolation,
  XT press/release events, modifier release after switching, and host edit controls.
  It does not boot a guest or require WHPX, but does require an interactive Windows desktop.

Runtime storage is `instances/<name>/config.json` and `overlay.qcow2`, with locks in the instances root.
Imported default overlays retain their original backing metadata; their config's baseImage specifies the reset base.
Both top-level adbHostPort and network.adbHostPort are saved consistently for earlier controller/config consumers.

## Design Decisions Log

| Decision | Chosen | Why |
|---|---|---|
| Hypervisor | WHPX (Hyper-V) | HAXM is deprecated; WHPX works on Win10/11 with Hyper-V |
| Display backend | Win32 SetParent with SDL/OpenGL | Reuses the existing QEMU display and avoids framebuffer copies; VNC is the fallback if reparenting is unstable |
| Input method | Native SDL mouse input and focused keyboard forwarding over QMP | Keyboard forwarding covers ordinary keys and mapped actions without relying on SDL's reparented keyboard path |
| Keymap profiles | JSON files beside the app | Users and later settings UI can change bindings without rebuilding the engine |
| Disk format | qcow2 overlay | Copy-on-write enables multi-instance + factory reset |
| App store | Neo Store (F-Droid client) | Uses F-Droid repositories without Google Play |
| Installer | Inno Setup | Free, mature, widely trusted on Windows |
| Build system | CMake | Cross-IDE, Qt 6 native support |
| Qt linking | Dynamic (LGPL) | Comply with Qt LGPL by dynamic linking |
