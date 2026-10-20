# Phase 1: VM Control — Detailed Implementation Guide

## Goal

Build the QEMU process manager and QMP client so we can
launch, stop, pause, resume, and snapshot an Android VM
from C++ code. No GUI yet — CLI test harness only.

---

## Prerequisites Checklist

Before coding, ensure you have:

- [ ] Visual Studio 2022 with C++ Desktop workload
- [ ] CMake 3.21+ (comes with VS, or install separately)
- [ ] Qt 6.5+ installed (need QtCore, QtNetwork)
  - Download: https://www.qt.io/download-open-source
  - Recommended: `C:\Qt\6.5.3\msvc2019_64`
- [ ] QEMU for Windows downloaded and extracted
  - Download: https://qemu.weilnetz.de/w64/
  - Extract to: `emulator\vm\qemu\`
  - Verify: `vm\qemu\qemu-system-x86_64.exe` exists
- [ ] A bootable disk image (any Linux/Android qcow2 for testing)
  - For quick testing, download a tiny Linux ISO like Alpine Linux
  - Or use a pre-built Bliss OS qcow2 if available

---

## Step-by-Step Implementation

### Step 1: Project Structure & CMake

Create the CMake build system that finds Qt and compiles our source files.

**`host-app/CMakeLists.txt`**:

```cmake
cmake_minimum_required(VERSION 3.21)
project(DroidBox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)  # Qt Meta-Object Compiler

# Find Qt packages
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# --- Main library (shared between app and tests) ---
add_library(droidbox_core STATIC
    src/qmp_client.cpp
    src/qemu_controller.cpp
)
target_include_directories(droidbox_core PUBLIC include)
target_link_libraries(droidbox_core PUBLIC Qt6::Core Qt6::Network)

# --- Main executable (placeholder for Phase 2 GUI) ---
add_executable(DroidBox src/main.cpp)
target_link_libraries(DroidBox PRIVATE droidbox_core)

# --- Test harness ---
add_executable(test_vm_control ../tests/test_vm_control.cpp)
target_link_libraries(test_vm_control PRIVATE droidbox_core)
```

Key decisions:
- `CMAKE_AUTOMOC ON` — Qt's MOC preprocessor runs automatically for
  files with `Q_OBJECT` macros
- Library `droidbox_core` — shared by the main app and tests
- Qt is found via `CMAKE_PREFIX_PATH` pointing to Qt install dir

---

### Step 2: QmpClient — The QMP Protocol Handler

QMP (QEMU Machine Protocol) is a JSON-based protocol for controlling QEMU.
It runs over a TCP socket. Here's how the protocol works:

```
1. QEMU opens a TCP server on the configured port (e.g., 4444)
2. Client connects
3. QEMU sends a greeting: {"QMP": {"version": {...}, "capabilities": [...]}}
4. Client sends: {"execute": "qmp_capabilities"}
5. QEMU responds: {"return": {}}
6. Now the client can send commands like:
   {"execute": "query-status"}
   → {"return": {"status": "running", "running": true}}

   {"execute": "stop"}
   → {"return": {}}

   {"execute": "cont"}
   → {"return": {}}

   {"execute": "quit"}
   → {"return": {}}
```

Each message is a single line of JSON terminated by newline.
Multiple messages may arrive in the same TCP read.

**Implementation notes for `QmpClient`:**

1. Use `QTcpSocket` for the TCP connection
2. Buffer incoming data and split on `\n` to handle partial reads
3. Parse each line as JSON
4. The handshake is: wait for greeting → send `qmp_capabilities`
5. After handshake, `execute()` sends a command and tracks its ID
6. QEMU may also send asynchronous events (e.g., `SHUTDOWN`, `STOP`)
   — route these to an `eventReceived` signal
7. Include a retry mechanism for initial connection (QEMU may take
   a few seconds to open the QMP port after launch)

**QMP command reference for this phase:**

| Command | Purpose | Returns |
|---|---|---|
| `qmp_capabilities` | Complete handshake | `{}` |
| `query-status` | Get VM state | `{"status": "running", "running": true}` |
| `stop` | Pause the VM | `{}` |
| `cont` | Resume the VM | `{}` |
| `system_powerdown` | Send ACPI shutdown | `{}` |
| `quit` | Ask QEMU to exit after an unhandled ACPI shutdown request | `{}` |
| `human-monitor-command` | Run HMP commands | varies |

#### Snapshot contract for WHPX

WHPX may not provide the dirty-memory tracking and device-state support
needed by QEMU's full `savevm`/`loadvm` VM snapshots. Phase 1 therefore
provides **qcow2 disk-only snapshots** using the bundled `qemu-img.exe`.
The VM must be fully stopped before either operation; a restored snapshot
restores disk contents only, and the guest boots again from that disk.
RAM, CPU, and device state are not saved.

The controller runs these equivalent commands against the configured
overlay while QEMU is stopped:

```powershell
vm\qemu\qemu-img.exe snapshot -c my_snapshot instances\default\overlay.qcow2
vm\qemu\qemu-img.exe snapshot -a my_snapshot instances\default\overlay.qcow2
```

Do not replace these with HMP `savevm`/`loadvm`: those attempt full VM-state
save/restore and fail on the project's WHPX configuration.

---

### Step 3: QemuController — Process Manager

This class builds the QEMU command line and manages the `QProcess`.

**Building the command line from JSON config:**

```cpp
QStringList QemuController::buildCommandLine(const QJsonObject& config) {
    QStringList args;

    // Acceleration — WHPX is mandatory
    args << "-accel" << "whpx,kernel-irqchip=off";

    // Memory
    args << "-m" << QString::number(config["ramMB"].toInt());

    // CPU cores
    args << "-smp" << QString::number(config["cpuCores"].toInt());

    // CPU model
    // Expose the CPU features supported by the active WHPX accelerator.
    // Bliss OS 14.10.3 requires SSE4.2, which qemu64 does not expose.
    args << "-cpu" << "max";

    // Disk — the overlay image (which references the base via backing file)
    QString overlay = config["overlayImage"].toString();
    args << "-drive" << QString("file=%1,format=qcow2,if=virtio").arg(overlay);

    // USB controller + tablet (for absolute mouse positioning)
    args << "-device" << "usb-ehci";
    args << "-device" << "usb-tablet";

    // Networking — user-mode NAT with ADB port forward
    int adbPort = config["adbHostPort"].toInt(5555);
    args << "-netdev" << QString("user,id=net0,hostfwd=tcp::%1-:5555").arg(adbPort);
    args << "-device" << "virtio-net-pci,netdev=net0";

    // Display — SDL for now (Phase 2 will embed or switch to VNC)
    args << "-display" << "sdl";

    // QMP — our control channel
    int qmpPort = config["qmpPort"].toInt(4444);
    args << "-qmp" << QString("tcp:localhost:%1,server,wait=off").arg(qmpPort);

    // Serial — useful for debugging boot issues
    args << "-serial" << "mon:stdio";

    // Boot from hard disk
    args << "-boot" << "c";

    return args;
}
```

**Process lifecycle:**

```
start(configPath):
    1. Read config JSON from file
    2. Validate paths (qemu exe exists? overlay exists?)
    3. Build command line
    4. Set QProcess working directory
    5. Start QProcess with qemu exe + args
    6. Start a retry timer to connect QMP (every 500ms, up to 30s)
    7. When QMP connects: emit vmStarted()

stop():
    1. Send "system_powerdown" via QMP (ACPI shutdown)
    2. Start a timeout timer (30 seconds)
    3. If QEMU exits within timeout: emit vmStopped(0)
    4. If the guest ignores ACPI powerdown, send QMP "quit" and wait 5 seconds
    5. If QEMU still does not exit: call kill()

kill():
    1. QProcess::kill() — force terminate
    2. Emit vmStopped(-1)
```

**Error detection:**

- QProcess::errorOccurred → QEMU binary not found, permission denied
- QProcess::finished with non-zero exit → crash or WHPX failure
- QMP connection failure after 30s → something is wrong
- Read QEMU's stderr for error messages (especially WHPX errors)

**Common QEMU error messages and user-friendly translations:**

| QEMU stderr | User message |
|---|---|
| `WHPX: Failed to initialize partition` | "Windows Hypervisor Platform is not available. Please enable VT-x in BIOS and enable the Windows Hypervisor Platform feature." |
| `Could not open 'overlay.qcow2'` | "The Android disk image was not found at: <path>. Please check your installation." |
| `Address already in use` | "Port <N> is already in use. Another instance may be running." |

---

### Step 4: Instance Config JSON

Create the default config template:

**`vm/configs/default-instance.json`**:
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

Note: Paths in the config are relative to the application directory.
The controller must resolve them to absolute paths using `QCoreApplication::applicationDirPath()`.

---

### Step 5: WHPX Check Script

**`vm/scripts/check-whpx.ps1`**:

This script checks two things:
1. CPU virtualization extensions (VT-x / AMD-V) enabled in BIOS
2. Windows Hypervisor Platform feature enabled in Windows

It outputs human-readable results and exits with code 0 (pass) or 1 (fail).

The script should also check for common conflicts:
- If VirtualBox is running (its hypervisor conflicts with WHPX)
- If Hyper-V is enabled but WHPX is not (they're different features)

---

### Step 6: File Logging

Set up application logging in `main.cpp`:

```cpp
// Install a custom message handler that writes to both
// a log file and stderr

void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    static QFile logFile("droidbox.log");
    static bool opened = logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString level;
    switch (type) {
        case QtDebugMsg:    level = "DEBUG"; break;
        case QtInfoMsg:     level = "INFO "; break;
        case QtWarningMsg:  level = "WARN "; break;
        case QtCriticalMsg: level = "ERROR"; break;
        case QtFatalMsg:    level = "FATAL"; break;
    }

    QString line = QString("[%1] [%2] %3\n").arg(timestamp, level, msg);

    // Write to file
    if (opened) {
        QTextStream stream(&logFile);
        stream << line;
        stream.flush();
    }

    // Also write to stderr
    fprintf(stderr, "%s", line.toLocal8Bit().constData());
}

int main(int argc, char* argv[]) {
    qInstallMessageHandler(messageHandler);
    // ...
}
```

---

### Step 7: CLI Test Harness

The test harness (`tests/test_vm_control.cpp`) is a console app that:

```
Usage: test_vm_control.exe [options]
Options:
  --config <path>    Instance config JSON (default: vm/configs/default-instance.json)
  --test <name>      Run specific test (default: all)
  --timeout <ms>     Timeout per operation (default: 30000)
  --boot-wait <ms>   Wait before graceful shutdown (default: 60000)

Tests:
  1. config_load    — Parse instance config JSON
  2. whpx_check     — Run WHPX detection
  3. qemu_launch    — Start QEMU process
  4. qmp_connect    — Connect to QMP socket
  5. query_status   — Query VM status
  6. pause_resume   — Pause then resume VM
  7. snapshot       — Stop QEMU, save/apply a disk-only qcow2 snapshot,
                      then boot the restored guest again
  8. graceful_stop  — ACPI shutdown, with QMP quit fallback for guests that ignore it
  9. force_kill     — Force kill QEMU process

Output:
  [PASS] config_load: Loaded config with 1 core, 4096 MB RAM
  [PASS] whpx_check: Virtualization enabled, WHPX available
  [PASS] qemu_launch: QEMU started (PID 12345)
  [PASS] qmp_connect: Connected to QMP on localhost:4444
  ...
```

Use `QCoreApplication` (not `QApplication`) since there's no GUI.
Use `QTimer::singleShot` for timeouts.
Use signals/slots to chain the test steps.

---

## Build Instructions

```powershell
# From the emulator/ root directory:

# Configure (adjust Qt path for your installation)
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64"

# Build
cmake --build build --config Release

# Run WHPX check first
powershell -ExecutionPolicy Bypass -File vm\scripts\check-whpx.ps1

# Run the test harness
.\build\Release\test_vm_control.exe --config vm\configs\default-instance.json
```

If you don't have a Bliss OS qcow2 yet, you can test with any bootable image:
1. Download Alpine Linux ISO: https://alpinelinux.org/downloads/
2. Create a test qcow2:
   ```powershell
   vm\qemu\qemu-img.exe create -f qcow2 test-disk.qcow2 8G
   ```
3. Modify the config to use the ISO as a CD-ROM for first boot

---

## Troubleshooting

| Problem | Likely Cause | Fix |
|---|---|---|
| QEMU exits immediately | WHPX not available | Run `check-whpx.ps1`, enable VT-x in BIOS |
| QMP won't connect | Port conflict or QEMU crashed | Check QEMU stderr, try different port |
| "kernel-irqchip" error | WHPX incompatibility | Try removing `kernel-irqchip=off` |
| Very slow boot | Using TCG instead of WHPX | Verify `-accel whpx` is accepted |
| Black screen | No bootable disk | Check overlay has a valid backing file |
