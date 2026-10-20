---
name: phase-4-adb-integration
description: >
  Implement Phase 4 of DroidBox: ADB Integration. Auto-connect ADB to the
  guest, implement APK install (drag-drop, button, file association), file
  push with media scan, screenshot capture, and navigation buttons.
---

# Phase 4: ADB Integration

## Overview

This phase connects all the stub toolbar buttons to real ADB commands.
The ADB bridge auto-connects to the guest Android when it boots, and
provides APK installation, file pushing, screenshots, and navigation.

## Before You Start

1. Read `docs/phase-guides/phase-4-adb-integration.md` for the detailed guide.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Phase 3 must be complete and tested.
4. You need `adb.exe` from Android platform-tools.

## What to Build

### Files to Create / Modify

| File | Purpose |
|---|---|
| `host-app/include/adb_bridge.h` | AdbBridge class declaration |
| `host-app/src/adb_bridge.cpp` | ADB command execution |
| `host-app/src/main_window.cpp` | **Modify**: wire toolbar buttons to ADB |
| `host-app/src/input_handler.cpp` | **Modify**: handle file drops |
| `host-app/CMakeLists.txt` | **Modify**: if needed |
| `tests/test_adb.cpp` | ADB integration tests |

### AdbBridge Class

```
Responsibilities:
- Find adb.exe (bundled path or PATH)
- Start/stop ADB server
- Connect to guest: adb connect localhost:5555
- Poll for device readiness (wait for boot_completed)
- Execute ADB commands via QProcess

Methods:
- setAdbPath(path)
- connectToGuest(host, port) -> async, emits connected/failed
- waitForBoot(timeoutMs) -> polls getprop sys.boot_completed
- installApk(apkPath) -> adb install -r <path>
- pushFile(localPath, remotePath) -> adb push
- mediaScan(remotePath) -> trigger MediaScanner
- screenshot(savePath) -> adb exec-out screencap -p > file
- shellCommand(cmd) -> adb shell <cmd>
- sendKeyEvent(keycode) -> adb shell input keyevent <code>
- isConnected() -> bool
- getDeviceSerial() -> QString

Signals:
- connected(serial)
- disconnected()
- bootCompleted()
- commandFinished(id, exitCode, stdout, stderr)
- installProgress(status)  // "uploading", "installing", "done", "failed"
- errorOccurred(message)
```

### Android Key Event Codes

| Toolbar Button | ADB Command |
|---|---|
| Home | `adb shell input keyevent 3` |
| Back | `adb shell input keyevent 4` |
| Recents | `adb shell input keyevent 187` |
| Volume Up | `adb shell input keyevent 24` |
| Volume Down | `adb shell input keyevent 25` |
| Power | `adb shell input keyevent 26` |

### APK Installation — Three Methods

#### 1. Drag-and-Drop
```
User drags .apk file onto DroidBox window
  → Qt dragEnterEvent checks for .apk extension
  → Qt dropEvent extracts file path
  → AdbBridge::installApk(path)
  → Show progress/result in status bar or popup
```

#### 2. "Install APK" Button
```
User clicks Install APK button in toolbar
  → QFileDialog::getOpenFileName(filter: "*.apk")
  → AdbBridge::installApk(selectedPath)
  → Show progress/result
```

#### 3. Windows File Association (set up in Phase 6 installer)
```
User double-clicks a .apk file in Windows Explorer
  → DroidBox.exe is launched with the .apk path as argv[1]
  → main() detects the .apk argument
  → If VM is running: installApk directly
  → If VM is not running: start VM, wait for boot, then install
```

For Phase 4, implement methods 1 and 2. Method 3's file association
registration happens in Phase 6 (installer), but handle the argv[1]
case in main.cpp now.

### File Push + Media Scan

```
User drags a non-APK file (image, video, document) onto the window:
  → adb push <local_path> /sdcard/Download/<filename>
  → adb shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE \
        -d file:///sdcard/Download/<filename>
  → File appears in Gallery / file manager immediately

The media scan broadcast makes Android index the new file so it shows
up in Gallery, the file manager, etc. Without it, pushed files are
invisible until the next full media scan (which may not happen for hours).
```

### Screenshot

```
AdbBridge::screenshot(savePath):
  → adb exec-out screencap -p > <savePath>
  → Save as PNG to user-chosen location (or default screenshots folder)
  → Show notification: "Screenshot saved to <path>"
```

### Auto-Connect Flow

```
QemuController emits vmStarted()
  → AdbBridge::connectToGuest("localhost", 5555)
  → Poll every 2 seconds: adb connect localhost:5555
  → Once connected: poll getprop sys.boot_completed
  → Once boot_completed == 1: emit bootCompleted()
  → Status bar updates: "Connecting..." → "Booting..." → "Ready"
```

### Error Handling

| Error | User Message |
|---|---|
| ADB not found | "adb.exe not found. Expected at: <path>. Please reinstall DroidBox." |
| Connection failed | "Cannot connect to Android guest. The VM may still be booting. Retrying..." |
| APK install failed | "Failed to install <name>.apk: <adb error message>" |
| File push failed | "Failed to copy <name> to Android: <adb error message>" |
| Screenshot failed | "Screenshot failed: <reason>" |

## Manual Test Checklist

- [ ] ADB auto-connects after VM boot (status shows "Ready")
- [ ] Home button sends HOME key event (Android goes to home screen)
- [ ] Back button sends BACK key event
- [ ] Recents button shows recent apps
- [ ] Volume buttons work
- [ ] Drag-dropping an APK file triggers installation
- [ ] APK install shows progress and success/failure message
- [ ] "Install APK" button opens file dialog and installs selected APK
- [ ] Drag-dropping an image file pushes it to /sdcard/Download
- [ ] Pushed image appears in Gallery app (media scan worked)
- [ ] Screenshot button saves a PNG file
- [ ] Screenshot matches what's on screen
- [ ] Starting DroidBox with an APK path as argument installs it
- [ ] Error messages are human-readable when ADB fails
- [ ] Status bar shows connection state throughout
