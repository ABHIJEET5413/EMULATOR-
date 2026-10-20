---
name: phase-2-display-input
description: >
  Implement Phase 2 of DroidBox: Display Embedding and Input. Embed the
  QEMU guest display inside the Qt main window and implement mouse/keyboard
  input forwarding. Build the side toolbar with stub buttons.
---

# Phase 2: Display Embedding & Input

## Overview

This phase creates the Qt main window, embeds the QEMU display inside it,
and forwards mouse/keyboard events to the guest. The user should be able
to interact with Android through the DroidBox window.

## Before You Start

1. Read `docs/phase-guides/phase-2-display-input.md` for the detailed guide.
2. Read `.agents/rules/project-rules.md` for coding standards.
3. Phase 1 must be complete and tested.

## What to Build

### Files to Create / Modify

| File | Purpose |
|---|---|
| `host-app/include/main_window.h` | Main window with toolbar and display area |
| `host-app/src/main_window.cpp` | Main window implementation |
| `host-app/include/display_embed.h` | QEMU display embedding widget |
| `host-app/src/display_embed.cpp` | Display embedding implementation |
| `host-app/include/input_handler.h` | Mouse/keyboard event translation |
| `host-app/src/input_handler.cpp` | Input handler implementation |
| `host-app/src/main.cpp` | **Modify**: launch GUI instead of CLI |
| `host-app/CMakeLists.txt` | **Modify**: add new files, Qt Widgets |
| `host-app/resources/icons/` | Toolbar icons (SVG or PNG) |

### Display Embedding Strategy

**Evaluate both approaches and pick one:**

#### Option A: Win32 SetParent (Recommended first try)
```
1. Launch QEMU with -display sdl (or gtk)
2. Find QEMU's window by title using Win32 EnumWindows
3. Call SetParent(qemuHwnd, qtWidgetHwnd) to reparent
4. Call SetWindowLong to remove QEMU's title bar and borders
5. Resize QEMU window to fill the Qt display area
6. Handle WM_SIZE to keep it in sync

Pros: Zero overhead, native performance, simple
Cons: Fragile (depends on QEMU's window title), may have focus issues
```

#### Option B: QEMU VNC + Qt VNC Widget
```
1. Launch QEMU with -display none -vnc :0
2. Connect a Qt-based VNC client to localhost:5900
3. Render the framebuffer in a QWidget
4. Send mouse/keyboard events via VNC protocol

Pros: Clean separation, works regardless of QEMU display backend
Cons: More code, VNC encoding/decoding overhead, latency
```

#### Option C: QEMU SPICE
```
1. Launch QEMU with SPICE display
2. Use a SPICE client library

Pros: Better than VNC for performance
Cons: SPICE on Windows is less mature, more dependencies
```

**Decision**: Try Option A first. If it works reliably, use it.
If not, fall back to Option B. Document the decision and why.

### Main Window Layout

```
┌─────────────────────────────────────────────┐
│  DroidBox - [instance name]            _ □ X │
├────┬────────────────────────────────────────┤
│ 🏠 │                                        │
│ ◄  │                                        │
│ ▣  │        QEMU Display Area               │
│ 🔊 │        (DisplayEmbed widget)            │
│ 🔄 │                                        │
│ 📷 │                                        │
│ 📦 │                                        │
│ ⛶  │                                        │
│ 📁 │                                        │
│ ⚙  │                                        │
├────┴────────────────────────────────────────┤
│  Status: Running | RAM: 4GB | ADB: ---      │
└─────────────────────────────────────────────┘
```

### Side Toolbar Buttons

| Button | Icon | Action (Phase 2) | Full Action (Later Phase) |
|---|---|---|---|
| Home | 🏠 | Stub (print to log) | ADB keyevent HOME (Phase 4) |
| Back | ◄ | Stub | ADB keyevent BACK (Phase 4) |
| Recents | ▣ | Stub | ADB keyevent RECENTS (Phase 4) |
| Volume Up | 🔊+ | Stub | ADB shell input (Phase 4) |
| Volume Down | 🔊- | Stub | ADB shell input (Phase 4) |
| Rotate | 🔄 | Stub | Change resolution (Phase 5) |
| Screenshot | 📷 | Stub | ADB screencap (Phase 4) |
| Install APK | 📦 | Stub | ADB install (Phase 4) |
| Fullscreen | ⛶ | Toggle fullscreen | — |
| Shared Folder | 📁 | Stub | ADB push (Phase 4) |
| Settings | ⚙ | Stub | Settings dialog (Phase 5) |

For Phase 2, only **Fullscreen** needs to actually work.
All other buttons should log a message like "Home button pressed (not yet connected to ADB)".

### Input Handler

```
Mouse Events:
- mousePressEvent   → QMP input-send-event (btn-down) or VNC pointer
- mouseReleaseEvent → QMP input-send-event (btn-up)
- mouseMoveEvent    → QMP input-send-event (motion, absolute coords)

Coordinate Translation:
- Qt widget coords → guest screen coords
- Must account for display scaling and offset

Keyboard Events:
- keyPressEvent   → QMP input-send-event (key-down, scancode)
- keyReleaseEvent → QMP input-send-event (key-up, scancode)
- Need a Qt key → HID scancode mapping table

If using VNC approach:
- Mouse/keyboard events are sent via the VNC protocol instead of QMP
```

### Classes

#### `MainWindow`
- Inherits `QMainWindow`
- Contains the side toolbar (`QToolBar` vertical) and central `DisplayEmbed`
- Status bar at the bottom
- Owns `QemuController` (from Phase 1) and creates `InputHandler`
- Handles drag-drop events (file drop → log for now, APK install in Phase 4)

#### `DisplayEmbed`
- Inherits `QWidget`
- If SetParent approach: holds the HWND of the embedded QEMU window
- If VNC approach: renders VNC framebuffer, handles resize
- Method: `embedWindow(HWND)` or `connectVnc(host, port)`
- Resizes embedded content when the widget resizes

#### `InputHandler`
- Takes a `QmpClient*` (or VNC client pointer)
- Installs event filter on `DisplayEmbed` to capture mouse/keyboard
- Translates coordinates and sends to guest
- Qt key → scancode mapping table (use a static map)

## Manual Test Checklist

- [ ] DroidBox window launches with the side toolbar visible
- [ ] QEMU starts and its display appears inside the DroidBox window
- [ ] No separate QEMU window is visible (it's embedded)
- [ ] Resizing DroidBox resizes the guest display
- [ ] Mouse clicks in the display area register as touches in Android
- [ ] Mouse movement is tracked correctly (cursor position matches)
- [ ] Keyboard typing works inside Android (e.g., in a text field)
- [ ] Fullscreen toggle works (F11 or button)
- [ ] All toolbar buttons are visible and log their actions
- [ ] Drag-dropping a file onto the window shows a log message
- [ ] Status bar shows VM status
- [ ] Window close triggers graceful VM shutdown
