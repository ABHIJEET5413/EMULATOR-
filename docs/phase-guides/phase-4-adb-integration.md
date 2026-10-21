# Phase 4: ADB Integration — Detailed Implementation Guide

## Goal

Connect ADB to the guest Android, wire up all toolbar buttons, implement
APK installation (drag-drop + button + argv), file push with media scan,
and screenshot capture.

---

## Prerequisites

- [ ] Phase 3 complete and tested
- [ ] `adb.exe` from Android SDK platform-tools
  - Download: https://developer.android.com/tools/releases/platform-tools
  - Extract to: `emulator\vm\adb\` (or `emulator\adb\`)
- [ ] A running Bliss OS instance with ADB enabled

---

## Step-by-Step Implementation

### Step 1: AdbBridge Class

The AdbBridge runs ADB commands as external processes via `QProcess`.
Each command is a separate `QProcess` invocation.

```cpp
// adb_bridge.h

class AdbBridge : public QObject {
    Q_OBJECT

public:
    explicit AdbBridge(QObject* parent = nullptr);

    /// Set the path to adb.exe
    void setAdbPath(const QString& path);

    /// Connect to the guest Android
    void connectToGuest(const QString& host, int port);

    /// Wait for Android to finish booting (polls getprop)
    void waitForBoot(int timeoutMs = 120000);

    /// Install an APK file
    void installApk(const QString& apkPath);

    /// Push a file to the guest
    void pushFile(const QString& localPath, const QString& remotePath);

    /// Trigger media scanner for a pushed file
    void mediaScan(const QString& remotePath);

    /// Take a screenshot and save locally
    void screenshot(const QString& savePath);

    /// Send a key event (e.g., HOME=3, BACK=4)
    void sendKeyEvent(int keycode);

    /// Run an arbitrary shell command
    void shellCommand(const QString& command);

    /// Check if connected
    bool isConnected() const;

signals:
    void connected(QString serial);
    void disconnected();
    void bootCompleted();
    void apkInstallFinished(bool success, QString message);
    void filePushFinished(bool success, QString message);
    void screenshotSaved(QString path);
    void errorOccurred(QString message);

private:
    /// Run an adb command and return (exitCode, stdout, stderr)
    struct AdbResult {
        int exitCode;
        QString stdout;
        QString stderr;
    };

    /// Synchronous ADB command (blocks, use for simple commands)
    AdbResult runAdb(const QStringList& args, int timeoutMs = 30000);

    /// Asynchronous ADB command (non-blocking)
    void runAdbAsync(const QStringList& args,
                     std::function<void(AdbResult)> callback);

    QString m_adbPath;
    QString m_serial;  // e.g., "localhost:5555"
    bool m_connected = false;
    QTimer* m_bootPollTimer = nullptr;
};
```

### Step 2: ADB Command Implementation

```cpp
// adb_bridge.cpp

AdbBridge::AdbResult AdbBridge::runAdb(const QStringList& args, int timeoutMs) {
    QProcess proc;
    proc.setProgram(m_adbPath);

    // Prepend -s <serial> if connected to a specific device
    QStringList fullArgs;
    if (!m_serial.isEmpty()) {
        fullArgs << "-s" << m_serial;
    }
    fullArgs << args;
    proc.setArguments(fullArgs);

    qDebug() << "ADB:" << m_adbPath << fullArgs.join(" ");

    proc.start();
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        return { -1, "", "ADB command timed out" };
    }

    return {
        proc.exitCode(),
        QString::fromUtf8(proc.readAllStandardOutput()).trimmed(),
        QString::fromUtf8(proc.readAllStandardError()).trimmed()
    };
}

void AdbBridge::connectToGuest(const QString& host, int port) {
    m_serial = QString("%1:%2").arg(host).arg(port);

    // Run in a separate thread to avoid blocking UI
    // Use QTimer for polling approach
    auto timer = new QTimer(this);
    timer->setInterval(2000);  // Try every 2 seconds

    int attempts = 0;
    connect(timer, &QTimer::timeout, this, [=]() mutable {
        attempts++;
        if (attempts > 30) {  // 60 seconds max
            timer->stop();
            timer->deleteLater();
            emit errorOccurred("Failed to connect ADB after 60 seconds");
            return;
        }

        auto result = runAdb({"connect", m_serial}, 5000);
        if (result.stdout.contains("connected")) {
            timer->stop();
            timer->deleteLater();
            m_connected = true;
            emit connected(m_serial);
            qInfo() << "ADB connected to" << m_serial;
        } else {
            qDebug() << "ADB connect attempt" << attempts << ":" << result.stdout;
        }
    });
    timer->start();
}

void AdbBridge::waitForBoot(int timeoutMs) {
    m_bootPollTimer = new QTimer(this);
    m_bootPollTimer->setInterval(3000);  // Check every 3 seconds

    QElapsedTimer elapsed;
    elapsed.start();

    connect(m_bootPollTimer, &QTimer::timeout, this, [=]() mutable {
        if (elapsed.elapsed() > timeoutMs) {
            m_bootPollTimer->stop();
            emit errorOccurred("Android boot timed out");
            return;
        }

        auto result = runAdb({"shell", "getprop", "sys.boot_completed"}, 5000);
        if (result.stdout.trimmed() == "1") {
            m_bootPollTimer->stop();
            emit bootCompleted();
            qInfo() << "Android boot completed";
        } else {
            qDebug() << "Waiting for boot... getprop returned:" << result.stdout;
        }
    });
    m_bootPollTimer->start();
}

void AdbBridge::installApk(const QString& apkPath) {
    qInfo() << "Installing APK:" << apkPath;

    // Run async so UI doesn't freeze
    runAdbAsync({"install", "-r", apkPath}, [this, apkPath](AdbResult result) {
        if (result.exitCode == 0 && result.stdout.contains("Success")) {
            qInfo() << "APK installed successfully:" << apkPath;
            emit apkInstallFinished(true, "Installed successfully");
        } else {
            QString error = result.stderr.isEmpty() ? result.stdout : result.stderr;
            qWarning() << "APK install failed:" << error;
            emit apkInstallFinished(false, error);
        }
    });
}

void AdbBridge::pushFile(const QString& localPath, const QString& remotePath) {
    QFileInfo fi(localPath);
    QString remoteFullPath = remotePath + fi.fileName();

    qInfo() << "Pushing file:" << localPath << "->" << remoteFullPath;

    runAdbAsync({"push", localPath, remoteFullPath}, [=](AdbResult result) {
        if (result.exitCode == 0) {
            emit filePushFinished(true, "File copied to " + remoteFullPath);
            // Trigger media scan so file shows up in Gallery
            mediaScan(remoteFullPath);
        } else {
            emit filePushFinished(false, result.stderr);
        }
    });
}

void AdbBridge::mediaScan(const QString& remotePath) {
    runAdb({"shell", "am", "broadcast",
            "-a", "android.intent.action.MEDIA_SCANNER_SCAN_FILE",
            "-d", "file://" + remotePath});
}

void AdbBridge::screenshot(const QString& savePath) {
    // Use exec-out to get raw PNG data
    QProcess proc;
    proc.setProgram(m_adbPath);
    QStringList args;
    if (!m_serial.isEmpty()) args << "-s" << m_serial;
    args << "exec-out" << "screencap" << "-p";
    proc.setArguments(args);
    proc.start();
    proc.waitForFinished(10000);

    QByteArray pngData = proc.readAllStandardOutput();
    if (pngData.isEmpty()) {
        emit errorOccurred("Screenshot failed: no data received");
        return;
    }

    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(pngData);
        file.close();
        emit screenshotSaved(savePath);
        qInfo() << "Screenshot saved to" << savePath;
    } else {
        emit errorOccurred("Cannot write screenshot to " + savePath);
    }
}

void AdbBridge::sendKeyEvent(int keycode) {
    runAdb({"shell", "input", "keyevent", QString::number(keycode)});
}
```

### Step 3: Wire Toolbar Buttons

Update `MainWindow` to connect buttons to `AdbBridge`:

```cpp
void MainWindow::onHome()       { m_adbBridge->sendKeyEvent(3); }   // KEYCODE_HOME
void MainWindow::onBack()       { m_adbBridge->sendKeyEvent(4); }   // KEYCODE_BACK
void MainWindow::onRecents()    { m_adbBridge->sendKeyEvent(187); } // KEYCODE_APP_SWITCH
void MainWindow::onVolUp()      { m_adbBridge->sendKeyEvent(24); }  // KEYCODE_VOLUME_UP
void MainWindow::onVolDown()    { m_adbBridge->sendKeyEvent(25); }  // KEYCODE_VOLUME_DOWN

void MainWindow::onScreenshot() {
    QString path = QFileDialog::getSaveFileName(
        this, "Save Screenshot", "screenshot.png", "PNG Images (*.png)");
    if (!path.isEmpty()) {
        m_adbBridge->screenshot(path);
    }
}

void MainWindow::onInstallApk() {
    QString path = QFileDialog::getOpenFileName(
        this, "Select APK", "", "Android Packages (*.apk)");
    if (!path.isEmpty()) {
        m_adbBridge->installApk(path);
    }
}
```

### Step 4: Drag-and-Drop (update from Phase 2)

```cpp
void MainWindow::dropEvent(QDropEvent* event) {
    if (!m_adbBridge->isConnected()) {
        QMessageBox::warning(this, "DroidBox",
            "Android is not ready yet. Please wait for boot to complete.");
        return;
    }

    for (const QUrl& url : event->mimeData()->urls()) {
        QString path = url.toLocalFile();
        if (path.endsWith(".apk", Qt::CaseInsensitive)) {
            m_adbBridge->installApk(path);
        } else {
            m_adbBridge->pushFile(path, "/sdcard/Download/");
        }
    }
}
```

### Step 5: APK from Command Line

Handle `.apk` file association (double-click opens DroidBox):

```cpp
// main.cpp
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Check if an APK was passed as argument
    QString apkToInstall;
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        if (args[i].endsWith(".apk", Qt::CaseInsensitive)) {
            apkToInstall = args[i];
            break;
        }
    }

    MainWindow window;
    window.show();

    // If APK was specified, install after boot
    if (!apkToInstall.isEmpty()) {
        QObject::connect(window.adbBridge(), &AdbBridge::bootCompleted,
            [&]() {
                window.adbBridge()->installApk(apkToInstall);
            });
    }

    return app.exec();
}
```

### Step 6: Status Bar Updates

```cpp
// In MainWindow constructor, connect signals to status updates:
connect(m_adbBridge, &AdbBridge::connected, this, [this](QString serial) {
    m_statusLabel->setText("Status: Connected (" + serial + ") — Booting...");
});

connect(m_adbBridge, &AdbBridge::bootCompleted, this, [this]() {
    m_statusLabel->setText("Status: Ready");
});

connect(m_adbBridge, &AdbBridge::apkInstallFinished, this,
    [this](bool success, QString msg) {
        if (success) {
            statusBar()->showMessage("APK installed successfully", 5000);
        } else {
            QMessageBox::warning(this, "Install Failed", msg);
        }
    });
```

---

## Testing

### Test ADB Manually First

Before integrating, verify ADB works with your running QEMU instance:

```powershell
# Start ADB server
vm\adb\adb.exe start-server

# Connect to the guest
vm\adb\adb.exe connect localhost:5555

# Check device
vm\adb\adb.exe devices

# Wait for boot
vm\adb\adb.exe -s localhost:5555 shell getprop sys.boot_completed

# Send a key event
vm\adb\adb.exe -s localhost:5555 shell input keyevent 3

# Install an APK
vm\adb\adb.exe -s localhost:5555 install -r some-app.apk

# Push a file
vm\adb\adb.exe -s localhost:5555 push test.jpg /sdcard/Download/

# Take a screenshot
# Preserve binary PNG bytes on Windows PowerShell 5.1 as well as PowerShell 7.
vm\adb\adb.exe -s localhost:5555 shell screencap -p /sdcard/Download/screenshot.png
vm\adb\adb.exe -s localhost:5555 pull /sdcard/Download/screenshot.png screenshot.png
```

If any of these fail, fix the VM/network config before coding the integration.

---

## Error Handling Notes

- ADB may disconnect if the VM pauses or hibernates → auto-reconnect
- APK install can fail for many reasons (incompatible arch, storage full,
  signature conflict) → show the actual ADB error message
- File push to `/sdcard/` may fail if the storage partition is full
- Screenshots fail if the guest hasn't fully booted yet
- Always check `isConnected()` before running commands
