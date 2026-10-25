#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QMimeData>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScreen>
#include <QToolBar>
#include <QUrl>
#include <QWidget>
#include <QLoggingCategory>

#include "display_embed.h"
#include "main_window.h"
#include "instance_manager.h"
#include "settings_dialog.h"

#include <utility>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Sabi"));
    setMinimumSize(480, 700);
    // Leave enough horizontal room for Android's first-run authorization
    // dialogs beside the vertical toolbar, even on a 1280px-wide desktop.
    resize(900, 850);
    setAcceptDrops(true);

    m_displayEmbed = new DisplayEmbed(this);
    setCentralWidget(m_displayEmbed);
    m_inputHandler.installOn(m_displayEmbed, &m_qemuController);

    createToolbar();

    m_statusLabel = new QLabel(QStringLiteral("Status: Starting"), this);
    statusBar()->addPermanentWidget(m_statusLabel);
    setAdbActionsEnabled(false);

    m_windowSearchTimer.setInterval(500);
    m_windowSearchTimeout.setSingleShot(true);
    m_closeWaitTimer.setSingleShot(true);

    connect(&m_qemuController, &QemuController::vmStarted,
            this, &MainWindow::beginDisplayWindowSearch);
    connect(&m_qemuController, &QemuController::vmStarted,
            this, [this]() {
                m_statusLabel->setText(QStringLiteral("Status: Connecting to Android"));
                m_adbBridge.connectToGuest(QStringLiteral("localhost"), m_qemuController.adbHostPort());
            });
    connect(&m_qemuController, &QemuController::vmStopped,
            this, [this](const int exitCode) {
                Q_UNUSED(exitCode)
                if (m_instanceManager) m_instanceManager->releaseInstance(m_instanceName);
                m_windowSearchTimer.stop();
                m_windowSearchTimeout.stop();
                m_adbBridge.disconnectFromGuest();
                setAdbActionsEnabled(false);
                m_displayEmbed->clearEmbeddedWindow();
                m_statusLabel->setText(QStringLiteral("Status: Stopped"));
                if (m_closePending) {
                    m_closeWaitTimer.stop();
                    m_allowClose = true;
                    QTimer::singleShot(0, this, &QWidget::close);
                }
            });
    connect(&m_qemuController, &QemuController::vmError,
            this, &MainWindow::handleVmError);
    connect(&m_adbBridge, &AdbBridge::connected, this,
            [this](const QString& serial) {
                m_statusLabel->setText(QStringLiteral("Status: Connected (%1), booting...").arg(serial));
            });
    connect(&m_adbBridge, &AdbBridge::authorizationRequired, this,
            [this](const QString& serial) {
                Q_UNUSED(serial)
                setAdbActionsEnabled(false);
                m_statusLabel->setText(QStringLiteral("Status: Select Allow in Android's USB debugging prompt"));
            });
    connect(&m_adbBridge, &AdbBridge::disconnected, this, [this]() {
        setAdbActionsEnabled(false);
        if (m_qemuController.isRunning()) {
            m_statusLabel->setText(QStringLiteral("Status: Reconnecting to Android"));
        }
    });
    connect(&m_adbBridge, &AdbBridge::bootCompleted, this, [this]() {
        applyLiveDisplaySettings();
        m_statusLabel->setText(QStringLiteral("Status: Ready"));
        setAdbActionsEnabled(true);
        installPendingApk();
    });
    connect(&m_adbBridge, &AdbBridge::installProgress, this, [this](const QString& progress) {
        if (progress == QStringLiteral("installing")) {
            statusBar()->showMessage(QStringLiteral("Installing APK..."));
        }
    });
    connect(&m_adbBridge, &AdbBridge::apkInstallFinished, this,
            [this](const bool success, const QString& message) {
                if (success) {
                    statusBar()->showMessage(message, 6000);
                } else {
                    QMessageBox::warning(this, QStringLiteral("APK installation failed"), message);
                }
            });
    connect(&m_adbBridge, &AdbBridge::filePushFinished, this,
            [this](const bool success, const QString& message) {
                if (success) {
                    statusBar()->showMessage(message, 6000);
                } else {
                    QMessageBox::warning(this, QStringLiteral("File copy failed"), message);
                }
            });
    connect(&m_adbBridge, &AdbBridge::screenshotSaved, this, [this](const QString& path) {
        statusBar()->showMessage(QStringLiteral("Screenshot saved to %1").arg(path), 6000);
    });
    connect(&m_adbBridge, &AdbBridge::errorOccurred,
            this, &MainWindow::handleAdbError);
    connect(&m_windowSearchTimer, &QTimer::timeout,
            this, &MainWindow::searchForQemuWindow);
    connect(&m_windowSearchTimeout, &QTimer::timeout,
            this, [this]() {
                m_windowSearchTimer.stop();
                if (!m_displayEmbed->hasEmbeddedWindow() && m_qemuController.isRunning()) {
                    m_statusLabel->setText(QStringLiteral("Status: Running (display not embedded)"));
                    QMessageBox::warning(
                        this,
                        QStringLiteral("QEMU display not found"),
                        QStringLiteral("QEMU is running, but Sabi could not find its SDL window within 30 seconds. "
                                       "Check that QEMU is using its SDL display backend and the same Windows privilege level."));
                }
            });
    connect(&m_closeWaitTimer, &QTimer::timeout,
            this, &MainWindow::handleCloseWaitTimeout);

    QTimer::singleShot(0, this, &MainWindow::startVm);
}

MainWindow::MainWindow(InstanceManager* manager, const QString& name, QWidget* parent)
    : MainWindow(parent)
{
    m_instanceManager = manager; m_instanceName = name;
    setWindowTitle("Sabi - " + name);
}

MainWindow::~MainWindow()
{
    m_qemuController.disconnect(this);
    m_adbBridge.disconnect(this);
    // The controller must finish with the disk before another process can claim it.
    m_qemuController.kill();
    if (m_qemuController.waitForExit() && m_instanceManager)
        m_instanceManager->releaseInstance(m_instanceName);
}

void MainWindow::createToolbar()
{
    auto* toolbar = new QToolBar(QStringLiteral("Controls"), this);
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setMinimumWidth(112);

    const auto addStub = [this, toolbar](const QString& label) {
        QAction* action = toolbar->addAction(label);
        connect(action, &QAction::triggered, this, [label]() {
            qInfo().noquote() << QStringLiteral("%1 button pressed (not yet connected).")
                                     .arg(label);
        });
    };

    const auto addKeyAction = [this, toolbar](const QString& label, const int keycode) {
        QAction* action = toolbar->addAction(label);
        m_adbActions.append(action);
        connect(action, &QAction::triggered, this, [this, keycode]() {
            m_adbBridge.sendKeyEvent(keycode);
        });
    };

    addKeyAction(QStringLiteral("Home"), 3);
    addKeyAction(QStringLiteral("Back"), 4);
    addKeyAction(QStringLiteral("Recents"), 187);
    toolbar->addSeparator();
    addKeyAction(QStringLiteral("Volume +"), 24);
    addKeyAction(QStringLiteral("Volume -"), 25);
    addKeyAction(QStringLiteral("Power"), 26);
    toolbar->addSeparator();
    QAction* rotateAction = toolbar->addAction(QStringLiteral("Rotate"));
    connect(rotateAction, &QAction::triggered, this, [this] {
        if (!m_instanceManager || m_instanceName.isEmpty()) return;
        auto config = m_instanceManager->getConfig(m_instanceName);
        auto display = config.value("display").toObject();
        const int width = display.value("width").toInt();
        const int height = display.value("height").toInt();
        if (width < 1 || height < 1) return;
        display["width"] = height;
        display["height"] = width;
        display["profile"] = QStringLiteral("custom");
        config["display"] = display;
        if (!m_instanceManager->setConfig(m_instanceName, config)) {
            QMessageBox::warning(this, QStringLiteral("Rotation not saved"), m_instanceManager->lastError());
            return;
        }
        m_launchConfig = config;
        if (m_displayProfile) m_displayProfile->setCurrentIndex(-1);
        applyLiveDisplaySettings();
    });
    QAction* screenshotAction = toolbar->addAction(QStringLiteral("Screenshot"));
    m_adbActions.append(screenshotAction);
    connect(screenshotAction, &QAction::triggered, this, &MainWindow::saveScreenshot);
    QAction* installAction = toolbar->addAction(QStringLiteral("Install APK"));
    m_adbActions.append(installAction);
    connect(installAction, &QAction::triggered, this, &MainWindow::installSelectedApk);
    toolbar->addSeparator();

    m_fullscreenAction = toolbar->addAction(QStringLiteral("Fullscreen"));
    m_fullscreenAction->setCheckable(true);
    connect(m_fullscreenAction, &QAction::triggered,
            this, [this](const bool enabled) {
                if (enabled) {
                    showFullScreen();
                } else {
                    showNormal();
                }
            });

    toolbar->addSeparator();
    m_displayProfile = new QComboBox(toolbar);
    m_displayProfile->setToolTip(QStringLiteral("Display profile; applies immediately to Android and persists for this instance."));
    m_displayProfile->addItem(QStringLiteral("Phone"), QStringLiteral("phone"));
    m_displayProfile->addItem(QStringLiteral("Tablet"), QStringLiteral("tablet"));
    m_displayProfile->addItem(QStringLiteral("PC"), QStringLiteral("pc"));
    m_displayProfile->addItem(QStringLiteral("Fullscreen"), QStringLiteral("fullscreen"));
    toolbar->addWidget(m_displayProfile);
    connect(m_displayProfile, &QComboBox::currentIndexChanged, this, [this] {
        if (m_displayProfile) applyDisplayProfile(m_displayProfile->currentData().toString());
    });

    toolbar->addSeparator();
    addStub(QStringLiteral("Shared Folder"));
    QAction* settings = toolbar->addAction(QStringLiteral("Settings"));
    connect(settings, &QAction::triggered, this, [this] {
        if (!m_instanceManager) return;
        SettingsDialog dialog(m_instanceManager->getConfig(m_instanceName), m_instanceManager->isRunning(m_instanceName), this);
        while (dialog.exec() == QDialog::Accepted) {
            if (m_instanceManager->setConfig(m_instanceName, dialog.getConfig())) {
                m_launchConfig = dialog.getConfig();
                const auto display = m_launchConfig.value("display").toObject();
                if (m_displayProfile) m_displayProfile->setCurrentIndex(m_displayProfile->findData(display.value("profile").toString()));
                applyLiveDisplaySettings();
                break;
            }
            QMessageBox::warning(this, "Settings not saved", m_instanceManager->lastError());
        }
    });
    connect(toolbar->addAction(QStringLiteral("Instances")), &QAction::triggered,
            this, &MainWindow::instanceListRequested);
    addToolBar(Qt::RightToolBarArea, toolbar);
}

QString MainWindow::findDefaultConfig() const
{
    const QString relativeConfig = QStringLiteral("vm/configs/default-instance.json");
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(relativeConfig),
        QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("../../%1").arg(relativeConfig)),
        QDir(QDir::currentPath()).filePath(relativeConfig)
    };

    for (const QString& candidate : candidates) {
        const QFileInfo fileInfo(QDir::cleanPath(candidate));
        if (fileInfo.exists() && fileInfo.isFile()) {
            return fileInfo.absoluteFilePath();
        }
    }
    return {};
}

void MainWindow::startVm()
{
    const QString configPath = m_instanceManager
        ? QDir(m_instanceManager->getInstanceDir(m_instanceName)).filePath("config.json") : findDefaultConfig();
    if (m_instanceManager) {
        m_launchConfig = m_instanceManager->getConfig(m_instanceName);
        // A preserved config can name the previous installation directory.
        // Use the manager's current runtime path when loading the disk config.
        m_qemuController.setQemuPath(m_launchConfig.value("qemuPath").toString());
        if (m_displayProfile) {
            const QString profile = m_launchConfig.value("display").toObject().value("profile").toString("custom");
            const int index = m_displayProfile->findData(profile);
            if (index >= 0) m_displayProfile->setCurrentIndex(index);
        }
    }
    if (configPath.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Status: Configuration missing"));
        QMessageBox::critical(
            this,
            QStringLiteral("VM configuration not found"),
            QStringLiteral("Could not find vm/configs/default-instance.json. "
                            "Run Sabi from the project folder or place the vm folder next to the application."));
        return;
    }

    const auto display = m_launchConfig.value("display").toObject();
    const QSize guestResolution(display.value("width").toInt(1080),
                                display.value("height").toInt(1920));
    if (guestResolution.width() > 0 && guestResolution.height() > 0) {
        m_displayEmbed->setGuestResolution(guestResolution);
        m_inputHandler.setGuestResolution(guestResolution);
        resizeForGuestDisplay(guestResolution);
    }

    m_statusLabel->setText(QStringLiteral("Status: Starting VM"));
    if (!m_qemuController.start(configPath)) {
        if (m_instanceManager) m_instanceManager->releaseInstance(m_instanceName);
        m_statusLabel->setText(QStringLiteral("Status: VM failed to start"));
    }
}

void MainWindow::applyDisplayProfile(const QString& profile)
{
    if (!m_instanceManager || m_instanceName.isEmpty() || profile.isEmpty()) return;
    auto config = m_instanceManager->getConfig(m_instanceName);
    auto display = config.value("display").toObject();
    if (display.value("profile").toString() == profile) return;
    int width = 1080, height = 2400, dpi = 420;
    if (profile == "tablet") { width = 1600; height = 2560; dpi = 280; }
    else if (profile == "pc") { width = 1920; height = 1080; dpi = 160; }
    else if (profile == "fullscreen") {
        QScreen* target = screen() ? screen() : QGuiApplication::primaryScreen();
        const QSize size = target ? target->size() : QSize(1920, 1080);
        width = size.width(); height = size.height(); dpi = 160;
    }
    display["profile"] = profile; display["width"] = width; display["height"] = height; display["dpi"] = dpi;
    config["display"] = display;
    if (!m_instanceManager->setConfig(m_instanceName, config)) {
        qWarning().noquote() << m_instanceManager->lastError();
        return;
    }
    m_launchConfig = config;
    applyLiveDisplaySettings();
}

void MainWindow::applyLiveDisplaySettings()
{
    const auto display = m_launchConfig.value("display").toObject();
    const QSize resolution(display.value("width").toInt(), display.value("height").toInt());
    if (resolution.width() < 1 || resolution.height() < 1) return;
    m_displayEmbed->setGuestResolution(resolution);
    m_inputHandler.setGuestResolution(resolution);
    resizeForGuestDisplay(resolution);
    if (m_adbBridge.isBootCompleted()) {
        m_adbBridge.shellCommand(QString("wm size %1x%2 && wm density %3")
            .arg(resolution.width()).arg(resolution.height()).arg(display.value("dpi").toInt()));
    }
}

void MainWindow::resizeForGuestDisplay(const QSize resolution)
{
    QScreen* targetScreen = this->screen();
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (!targetScreen || resolution.width() < 1 || resolution.height() < 1) {
        return;
    }

    const QRect available = targetScreen->availableGeometry();
    // Leave room for native window decoration, status bar, and the fixed
    // toolbar. The guest remains aspect-correct inside the remaining client
    // area, including on displays smaller than the chosen preset.
    constexpr int chromeWidth = 150;
    constexpr int chromeHeight = 110;
    const QSize maximum(qMax(480, available.width() - chromeWidth - 32),
                        qMax(700, available.height() - chromeHeight - 32));
    const qreal scale = qMin<qreal>(1.0,
                                    qMin(static_cast<qreal>(maximum.width()) / resolution.width(),
                                         static_cast<qreal>(maximum.height()) / resolution.height()));
    const QSize guestSize(qMax(240, qRound(resolution.width() * scale)),
                          qMax(320, qRound(resolution.height() * scale)));
    resize(guestSize.width() + chromeWidth, guestSize.height() + chromeHeight);
}

void MainWindow::beginDisplayWindowSearch()
{
    m_statusLabel->setText(QStringLiteral("Status: Running (locating display)"));
    m_windowSearchTimeout.start(30000);
    m_windowSearchTimer.start();
    searchForQemuWindow();
}

void MainWindow::searchForQemuWindow()
{
    if (!m_qemuController.isRunning()) {
        m_windowSearchTimer.stop();
        m_windowSearchTimeout.stop();
        return;
    }

    const quintptr qemuWindow = DisplayEmbed::findWindowForProcess(m_qemuController.processId());
    if (qemuWindow == 0) {
        return;
    }

    QString errorMessage;
    if (!m_displayEmbed->embedWindow(qemuWindow, &errorMessage)) {
        m_windowSearchTimer.stop();
        m_windowSearchTimeout.stop();
        m_statusLabel->setText(QStringLiteral("Status: Running (display not embedded)"));
        QMessageBox::warning(this,
                             QStringLiteral("Could not embed QEMU display"),
                             errorMessage);
        return;
    }

    m_windowSearchTimer.stop();
    m_windowSearchTimeout.stop();
    m_statusLabel->setText(QStringLiteral("Status: Running"));
}

void MainWindow::handleVmError(const QString& message)
{
    m_statusLabel->setText(QStringLiteral("Status: VM error"));
    QMessageBox::critical(this, QStringLiteral("Sabi VM error"), message);
}

void MainWindow::handleAdbError(const QString& message)
{
    qWarning().noquote() << message;
    if (!m_adbBridge.isBootCompleted()) {
        m_statusLabel->setText(QStringLiteral("Status: Android unavailable"));
    }
    QMessageBox::warning(this, QStringLiteral("Sabi Android connection"), message);
}

void MainWindow::setAdbActionsEnabled(const bool enabled)
{
    for (QAction* action : std::as_const(m_adbActions)) {
        if (action) {
            action->setEnabled(enabled);
        }
    }
}

void MainWindow::installSelectedApk()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select APK to install"),
        QDir::homePath(),
        QStringLiteral("Android packages (*.apk)"));
    if (!path.isEmpty()) {
        m_adbBridge.installApk(path);
    }
}

void MainWindow::saveScreenshot()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save Android screenshot"),
        QDir(directory).filePath(QStringLiteral("screenshot.png")),
        QStringLiteral("PNG images (*.png)"));
    if (!path.isEmpty()) {
        m_adbBridge.screenshot(path);
    }
}

void MainWindow::queueApkInstall(const QString& apkPath)
{
    const QFileInfo apkInfo(apkPath);
    if (!apkInfo.exists() || !apkInfo.isFile()
        || apkInfo.suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) != 0) {
        QMessageBox::warning(this,
                             QStringLiteral("APK not found"),
                             QStringLiteral("The command-line APK path is not an existing .apk file: %1")
                                 .arg(QDir::toNativeSeparators(apkPath)));
        return;
    }

    m_pendingApkPath = apkInfo.absoluteFilePath();
    if (m_adbBridge.isBootCompleted()) {
        installPendingApk();
    }
}

void MainWindow::installPendingApk()
{
    if (m_pendingApkPath.isEmpty()) {
        return;
    }
    const QString apkPath = m_pendingApkPath;
    m_pendingApkPath.clear();
    m_adbBridge.installApk(apkPath);
}

void MainWindow::handleCloseWaitTimeout()
{
    if (!m_closePending || !m_qemuController.isRunning()) {
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("Sabi"),
        QStringLiteral("The Android VM is still shutting down. Force close it?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (choice == QMessageBox::Yes) {
        m_qemuController.kill();
    } else {
        m_closePending = false;
        m_statusLabel->setText(QStringLiteral("Status: Shutting down"));
    }
}

void MainWindow::updateFullscreenAction()
{
    if (m_fullscreenAction) {
        m_fullscreenAction->setChecked(isFullScreen());
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_allowClose || !m_qemuController.isRunning()) {
        event->accept();
        return;
    }

    event->ignore();
    if (m_closePending) {
        return;
    }

    m_closePending = true;
    m_statusLabel->setText(QStringLiteral("Status: Shutting down"));
    m_closeWaitTimer.start(35000);
    m_qemuController.stop();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateFullscreenAction();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!m_adbBridge.isBootCompleted()) {
        QMessageBox::warning(this,
                             QStringLiteral("Android is not ready"),
                             QStringLiteral("Wait for the Android guest to finish booting before dropping files."));
        event->ignore();
        return;
    }

    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) {
            continue;
        }
        if (QFileInfo(path).suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) == 0) {
            m_adbBridge.installApk(path);
        } else {
            m_adbBridge.pushFile(path, QStringLiteral("/sdcard/Download/"));
        }
    }
    event->acceptProposedAction();
}
