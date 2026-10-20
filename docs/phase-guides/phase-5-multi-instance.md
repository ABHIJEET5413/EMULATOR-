# Phase 5: Multi-Instance, Settings & Camera — Detailed Implementation Guide

## Goal

Enable multiple Android instances (each with its own qcow2 overlay),
build a settings dialog for per-instance configuration, and evaluate
camera support options.

---

## Prerequisites

- [ ] Phase 4 complete and tested
- [ ] `qemu-img.exe` available (comes with QEMU)
- [ ] Understanding of qcow2 overlay/backing-file mechanism

---

## Part A: Multi-Instance Manager

### How qcow2 Overlays Work

QEMU's qcow2 format supports **backing files**: an overlay image stores
only the blocks that differ from its base. This means:

- Creating a new instance = creating a tiny overlay file (~200 KB)
- All instances share the same base image (read-only)
- Each instance's changes are isolated in its overlay
- Factory reset = delete overlay + create a new empty one
- Cloning = copy the overlay file

```
bliss-base.qcow2  (2-4 GB, read-only, shared)
    │
    ├── instances/default/overlay.qcow2   (changes only)
    ├── instances/work/overlay.qcow2      (changes only)
    └── instances/gaming/overlay.qcow2    (changes only)
```

### Creating an Overlay

```cpp
bool InstanceManager::createOverlay(const QString& overlayPath,
                                     const QString& basePath) {
    QProcess proc;
    proc.setProgram(m_qemuImgPath);  // path to qemu-img.exe
    proc.setArguments({
        "create", "-f", "qcow2",
        "-b", basePath,
        "-F", "qcow2",
        overlayPath
    });
    proc.start();
    proc.waitForFinished(30000);

    if (proc.exitCode() != 0) {
        qWarning() << "Failed to create overlay:" << proc.readAllStandardError();
        return false;
    }
    return true;
}
```

### Instance Directory Structure

```
instances/
├── default/
│   ├── config.json       # Instance-specific settings
│   └── overlay.qcow2     # Copy-on-write disk overlay
├── gaming/
│   ├── config.json
│   └── overlay.qcow2
└── work/
    ├── config.json
    └── overlay.qcow2
```

### InstanceManager Implementation

```cpp
class InstanceManager : public QObject {
    Q_OBJECT

public:
    struct InstanceInfo {
        QString name;
        QString path;         // Full path to instance directory
        bool isRunning;
        int cpuCores;
        int ramMB;
        QString resolution;
        qint64 overlaySize;   // Bytes used by overlay
    };

    /// List all instances found in the instances directory
    QList<InstanceInfo> listInstances() const;

    /// Create a new instance with given name and config
    bool createInstance(const QString& name, const QJsonObject& config = {});

    /// Clone an existing instance
    bool cloneInstance(const QString& sourceName, const QString& newName);

    /// Delete an instance (removes directory)
    bool deleteInstance(const QString& name);

    /// Factory reset: delete overlay, create fresh one
    bool factoryReset(const QString& name);

    /// Check if a name is valid (alphanumeric, no spaces, not duplicate)
    bool isValidName(const QString& name) const;

    /// Allocate unique ports for a new instance
    QPair<int, int> allocatePorts() const;  // returns (qmpPort, adbPort)
};
```

### Port Allocation

Each instance needs unique QMP and ADB ports:

```cpp
QPair<int, int> InstanceManager::allocatePorts() const {
    QSet<int> usedQmp, usedAdb;
    for (const auto& inst : listInstances()) {
        QJsonObject config = loadConfig(inst.name);
        usedQmp.insert(config["qmpPort"].toInt());
        usedAdb.insert(config["adbHostPort"].toInt());
    }

    int qmpPort = 4444;
    while (usedQmp.contains(qmpPort)) qmpPort++;

    int adbPort = 5555;
    while (usedAdb.contains(adbPort)) adbPort++;

    return { qmpPort, adbPort };
}
```

### Instance List Window

The instance list is shown on startup (if multiple instances exist)
or accessible from a menu:

```cpp
class InstanceListWindow : public QWidget {
    Q_OBJECT

public:
    explicit InstanceListWindow(InstanceManager* manager, QWidget* parent = nullptr);

signals:
    void instanceLaunched(QString name);

private:
    void refreshList();
    void onCreateNew();
    void onLaunch(const QString& name);
    void onConfigure(const QString& name);
    void onClone(const QString& name);
    void onDelete(const QString& name);
    void onFactoryReset(const QString& name);

    InstanceManager* m_manager;
    QListWidget* m_listWidget;
};
```

---

## Part B: Settings Dialog

### Settings Dialog Implementation

```cpp
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(const QJsonObject& config, QWidget* parent = nullptr);

    /// Get the modified config
    QJsonObject getConfig() const;

private:
    void setupUi();
    void loadFromConfig(const QJsonObject& config);
    bool validate();

    // Performance
    QSpinBox* m_cpuCores;
    QSlider* m_ramSlider;
    QLabel* m_ramLabel;

    // Display
    QComboBox* m_resolution;
    QComboBox* m_dpi;

    // Camera
    QButtonGroup* m_cameraMode;

    QJsonObject m_originalConfig;
};
```

### Validation Rules

```cpp
bool SettingsDialog::validate() {
    // CPU cores: 1 to host core count
    int hostCores = QThread::idealThreadCount();
    if (m_cpuCores->value() > hostCores) {
        QMessageBox::warning(this, "Invalid",
            QString("CPU cores cannot exceed %1 (your PC has %1 cores)")
            .arg(hostCores));
        return false;
    }

    // RAM: at least 1024, at most half of host RAM
    // (Getting host RAM requires platform-specific code)

    return true;
}
```

### Resolution Presets

```
Phone:    720  × 1280  (HD,     DPI 240)
Phone:    1080 × 1920  (FHD,    DPI 320)
Phone:    1440 × 2560  (QHD,    DPI 480)
Tablet:   1280 × 800   (WXGA,   DPI 160)
Tablet:   1920 × 1200  (FHD,    DPI 240)
Tablet:   2560 × 1600  (QHD,    DPI 320)
```

Note: Resolution changes require restarting the QEMU instance.
The QEMU command line includes resolution as part of the `-device virtio-gpu`
or display configuration.

---

## Part C: Camera

### Create `docs/CAMERA.md`

This document must be an honest evaluation. Research and document:

#### 1. Does the Android Camera app crash without any camera device?
- Test this with Bliss OS: open Camera app
- If it crashes: what error? (Check `adb logcat`)
- Can we prevent the crash by installing a camera-less Camera app?

#### 2. Can QEMU emulate a camera device?
- QEMU does NOT have a built-in virtual camera for Windows
- On Linux, QEMU can use `-device usb-video` with v4l2, but this
  doesn't exist on Windows
- Research if any QEMU fork adds Windows camera support

#### 3. USB Webcam Passthrough
```
QEMU args for USB passthrough:
  -device usb-host,vendorid=0x046d,productid=0x082d

Steps for the user:
1. Find webcam VID:PID in Device Manager
2. Install WinUSB driver via Zadig (replaces the webcam's driver)
3. Add the USB passthrough args to QEMU
4. The webcam now appears inside Android

Problems:
- Zadig replaces the driver → webcam stops working in Windows
- Need to revert the driver to use the webcam in Windows again
- Not all webcams are compatible
- QEMU may crash with some USB devices on Windows
```

#### 4. Recommendations
```
Default:  camera.mode = "none"
          Camera app may crash → acceptable with a warning

Optional: camera.mode = "webcam" (experimental)
          Document the Zadig process
          Mark as "advanced users only"

Future:   Research VirtIO camera device (in development upstream)
```

### Camera Config Implementation

If camera mode is "webcam", add USB passthrough args:

```cpp
if (config["camera"]["mode"] == "webcam") {
    // User must configure VID:PID in settings
    QString vid = config["camera"]["vendorId"].toString();
    QString pid = config["camera"]["productId"].toString();
    args << "-device" << QString("usb-host,vendorid=%1,productid=%2").arg(vid, pid);
}
```

---

## Application Startup Flow (Updated)

```
main.cpp:
  if (argc > 1 && argv[1] is .apk):
      Launch default instance, install APK on boot
  else if (only one instance exists):
      Launch it directly (MainWindow)
  else:
      Show InstanceListWindow
      User picks an instance → launch MainWindow for it
```

---

## Manual Test Checklist

See the SKILL.md for the complete checklist. Key items:

- [ ] Create instance → overlay file exists, small size
- [ ] Launch two instances → different ports, both work
- [ ] Factory reset → overlay is fresh, Android shows setup
- [ ] Settings change → config.json updated, takes effect on restart
- [ ] Camera section of CAMERA.md is honest and complete
