# Phase 2: Display Embedding & Input — Detailed Implementation Guide

## Goal

Embed the QEMU guest display inside the Qt main window so DroidBox looks
like a single application. Forward mouse and keyboard events to the guest.
Build the side toolbar with stub buttons.

---

## Prerequisites

- [ ] Phase 1 complete and tested (QEMU launches, QMP works)
- [ ] Qt 6.5+ with QtWidgets module
- [ ] Familiarity with Win32 API basics (SetParent, FindWindow)

---

## Step-by-Step Implementation

### Step 1: Update CMakeLists.txt

Add Qt6::Widgets and the new source files:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Gui)

add_library(droidbox_core STATIC
    src/qmp_client.cpp
    src/qemu_controller.cpp
    src/display_embed.cpp
    src/input_handler.cpp
    src/main_window.cpp
)
target_link_libraries(droidbox_core PUBLIC Qt6::Core Qt6::Network Qt6::Widgets Qt6::Gui)

# On Windows, link user32 for SetParent etc.
if(WIN32)
    target_link_libraries(droidbox_core PUBLIC user32)
endif()
```

---

### Step 2: Display Embedding via Win32 SetParent

This is the recommended approach. Here's how it works:

#### Finding the QEMU Window

QEMU with `-display sdl` creates a window titled like:
`"QEMU (instance-name) - SDL"` or `"QEMU"`.

```cpp
// Use EnumWindows + GetWindowText to find it
struct FindWindowData {
    DWORD processId;
    HWND result;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto data = reinterpret_cast<FindWindowData*>(lParam);
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == data->processId) {
        // Verify it's a visible, top-level window
        if (IsWindowVisible(hwnd) && GetParent(hwnd) == nullptr) {
            data->result = hwnd;
            return FALSE; // Stop enumeration
        }
    }
    return TRUE; // Continue
}
```

We find the window by QEMU's process ID (from QProcess::processId()),
not by title, so it works regardless of QEMU version/locale.

#### Reparenting

```cpp
void DisplayEmbed::embedWindow(HWND qemuHwnd) {
    m_qemuHwnd = qemuHwnd;

    // Get the Qt widget's native window handle
    HWND parentHwnd = reinterpret_cast<HWND>(this->winId());

    // Remove QEMU's title bar and borders
    LONG style = GetWindowLong(qemuHwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_CHILD;
    SetWindowLong(qemuHwnd, GWL_STYLE, style);

    // Reparent
    SetParent(qemuHwnd, parentHwnd);

    // Resize to fill the widget
    resizeEmbeddedWindow();
}

void DisplayEmbed::resizeEmbeddedWindow() {
    if (m_qemuHwnd) {
        QSize sz = this->size();
        MoveWindow(m_qemuHwnd, 0, 0, sz.width(), sz.height(), TRUE);
    }
}

// Override resizeEvent to keep QEMU window in sync
void DisplayEmbed::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    resizeEmbeddedWindow();
}
```

#### Timing: When to Embed

QEMU's window doesn't appear immediately. You need to poll for it:

```cpp
// After QemuController emits vmStarted(), start polling
void MainWindow::onVmStarted() {
    m_embedTimer = new QTimer(this);
    m_embedTimer->setInterval(500); // check every 500ms
    connect(m_embedTimer, &QTimer::timeout, this, [this]() {
        HWND hwnd = findQemuWindow(m_qemuController->processId());
        if (hwnd) {
            m_embedTimer->stop();
            m_displayEmbed->embedWindow(hwnd);
            qInfo() << "QEMU window embedded successfully";
        }
    });
    m_embedTimer->start();

    // Timeout after 30 seconds
    QTimer::singleShot(30000, this, [this]() {
        if (m_embedTimer->isActive()) {
            m_embedTimer->stop();
            qWarning() << "Failed to find QEMU window after 30 seconds";
            // Fall back to separate window or show error
        }
    });
}
```

#### Known Issues with SetParent

| Issue | Mitigation |
|---|---|
| Focus fights between Qt and QEMU | Forward focus events manually |
| QEMU may resize itself | Override WM_SIZE in the embedded window |
| Keyboard input may go to wrong window | Use SetFocus when user clicks display |
| QEMU window may disappear on minimize | Show/hide with parent |

If SetParent proves too unreliable, **switch to the VNC approach**:

### Step 2 Alternative: VNC Approach

If SetParent doesn't work well:

1. Launch QEMU with `-display none -vnc :0`
   (VNC server on port 5900)
2. Implement a minimal VNC client in Qt:
   - Connect to `localhost:5900`
   - Perform RFB handshake
   - Receive framebuffer updates
   - Render to a `QWidget` using `QPainter`
   - Send mouse/keyboard events via VNC protocol
3. This is more code but completely reliable

There are also Qt VNC client libraries you could use.
Document which approach you chose and why.

---

### Step 3: MainWindow Layout

```cpp
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("DroidBox");
    setMinimumSize(400, 700);
    resize(450, 800);  // Phone-like aspect ratio

    // Central widget: the display embed area
    m_displayEmbed = new DisplayEmbed(this);
    setCentralWidget(m_displayEmbed);

    // Side toolbar (right side, vertical)
    createToolbar();

    // Status bar
    m_statusBar = statusBar();
    m_statusLabel = new QLabel("Status: Stopped");
    m_statusBar->addWidget(m_statusLabel);

    // Accept drag-and-drop
    setAcceptDrops(true);
}
```

#### Toolbar Creation

```cpp
void MainWindow::createToolbar() {
    QToolBar* toolbar = new QToolBar("Controls", this);
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(32, 32));

    // Use built-in Qt icons or load from resources
    toolbar->addAction(QIcon(":/icons/home.png"), "Home", this, &MainWindow::onHome);
    toolbar->addAction(QIcon(":/icons/back.png"), "Back", this, &MainWindow::onBack);
    toolbar->addAction(QIcon(":/icons/recents.png"), "Recents", this, &MainWindow::onRecents);
    toolbar->addSeparator();
    toolbar->addAction(QIcon(":/icons/vol-up.png"), "Vol +", this, &MainWindow::onVolUp);
    toolbar->addAction(QIcon(":/icons/vol-down.png"), "Vol -", this, &MainWindow::onVolDown);
    toolbar->addSeparator();
    toolbar->addAction(QIcon(":/icons/rotate.png"), "Rotate", this, &MainWindow::onRotate);
    toolbar->addAction(QIcon(":/icons/screenshot.png"), "Screenshot", this, &MainWindow::onScreenshot);
    toolbar->addAction(QIcon(":/icons/install.png"), "Install APK", this, &MainWindow::onInstallApk);
    toolbar->addSeparator();
    toolbar->addAction(QIcon(":/icons/fullscreen.png"), "Fullscreen", this, &MainWindow::onFullscreen);
    toolbar->addAction(QIcon(":/icons/folder.png"), "Shared Folder", this, &MainWindow::onSharedFolder);
    toolbar->addAction(QIcon(":/icons/settings.png"), "Settings", this, &MainWindow::onSettings);

    addToolBar(Qt::RightToolBarArea, toolbar);
}
```

For Phase 2, button handlers just log:
```cpp
void MainWindow::onHome() {
    qInfo() << "Home button pressed (ADB not connected yet)";
}
```

Except **Fullscreen**, which should work:
```cpp
void MainWindow::onFullscreen() {
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}
```

---

### Step 4: Input Handler

The InputHandler installs an event filter on the DisplayEmbed widget.

```cpp
bool InputHandler::eventFilter(QObject* obj, QEvent* event) {
    if (obj != m_displayWidget) return false;

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        return handleMouseEvent(static_cast<QMouseEvent*>(event));

    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        return handleKeyEvent(static_cast<QKeyEvent*>(event));

    default:
        return false;
    }
}
```

#### Mouse → QMP Touch Events

If using the SetParent approach, mouse events naturally reach the
embedded QEMU window and you may not need to manually forward them.

If using VNC, you send RFB pointer events.

If you need to use QMP for input:
```json
{"execute": "input-send-event",
 "arguments": {"events": [
   {"type": "abs", "data": {"axis": "x", "value": 15000}},
   {"type": "abs", "data": {"axis": "y", "value": 20000}},
   {"type": "btn", "data": {"button": "left", "down": true}}
 ]}}
```

Note: QMP absolute coordinates use a 0-32767 range.
Scale from widget coordinates:
```cpp
int qmpX = (mouseX * 32767) / widgetWidth;
int qmpY = (mouseY * 32767) / widgetHeight;
```

#### Keyboard → Scancodes

For QMP input, you need HID scancodes:
```json
{"execute": "input-send-event",
 "arguments": {"events": [
   {"type": "key", "data": {"key": {"type": "number", "data": 28}, "down": true}}
 ]}}
```

Where `28` is the HID scancode for Enter.

You'll need a Qt key → HID scancode mapping. See:
https://gist.github.com/MightyPork/6da26e382a7ad91b5496ee55fdc73db2

If using SetParent, keyboard events should reach QEMU naturally when
the embedded window has focus. Test this first!

---

### Step 5: Drag-and-Drop

```cpp
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".apk", Qt::CaseInsensitive)) {
            qInfo() << "APK dropped:" << path << "(install not yet implemented)";
            // Phase 4: m_adbBridge->installApk(path);
        } else {
            qInfo() << "File dropped:" << path << "(push not yet implemented)";
            // Phase 4: m_adbBridge->pushFile(path, "/sdcard/Download/");
        }
    }
}
```

---

### Step 6: Window Close = VM Shutdown

```cpp
void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_qemuController->isRunning()) {
        // Try graceful shutdown
        m_qemuController->stop();

        // If it takes too long, ask user
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "DroidBox",
            "The Android VM is still shutting down. Force close?",
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            m_qemuController->kill();
        } else {
            event->ignore();
            return;
        }
    }
    event->accept();
}
```

---

## Toolbar Icons

For Phase 2, you can use any of these icon sources:
- Qt's built-in `QStyle::standardIcon()` for basic icons
- Simple text labels as placeholders
- Free icon sets (Material Design Icons, Feather Icons)
- Generate simple SVG icons

The icons will be polished in later phases.

---

## Build Instructions

```powershell
cmake -B build -S host-app -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64"

cmake --build build --config Release

.\build\Release\DroidBox.exe
```

---

## Troubleshooting

| Problem | Fix |
|---|---|
| QEMU window not found | Check processId, add delay, check window enumeration |
| SetParent returns 0 | Check UIPI (run DroidBox as admin if QEMU is elevated) |
| Input not reaching QEMU | Check focus, try SetFocus on embedded window |
| Black embedded area | QEMU may use DirectX; try `-display sdl` or `-display gtk` |
| Keyboard shortcuts conflict | Qt toolbar shortcuts may intercept keys; use event filters |
