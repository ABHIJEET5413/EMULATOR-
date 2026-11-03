#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QGuiApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPixmap>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScreen>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QLoggingCategory>

#include "display_embed.h"
#include "icon_factory.h"
#include "input_diagnostics_overlay.h"
#include "keymap_overlay.h"
#include "keymap_studio_dialog.h"
#include "main_window.h"
#include "instance_manager.h"
#include "performance_monitor.h"
#include "runtime_recovery.h"
#include "settings_dialog.h"
#include "ui_theme.h"

#include <utility>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    UiTheme::polishWindow(this, QStringLiteral("sabiMainWindow"));
    setWindowTitle(QStringLiteral("Sabi"));
    setMinimumSize(480, 700);
    // Leave enough horizontal room for Android's first-run authorization
    // dialogs beside the vertical toolbar, even on a 1280px-wide desktop.
    resize(900, 850);
    setAcceptDrops(true);

    auto* shell = new QWidget(this);
    shell->setObjectName(QStringLiteral("sabiShell"));
    auto* shellLayout = new QVBoxLayout(shell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);
    createContextBar();
    m_displayEmbed = new DisplayEmbed(shell);
    shellLayout->addWidget(m_displayEmbed, 1);
    setCentralWidget(shell);
    m_inputHandler.installOn(m_displayEmbed, &m_qemuController);
    m_inputDiagnosticsOverlay = new InputDiagnosticsOverlay(m_displayEmbed);
    m_keymapOverlay = new KeymapOverlay(m_displayEmbed);
    connect(m_displayEmbed, &DisplayEmbed::viewportChanged,
            this, &MainWindow::refreshInputDiagnostics);
    connect(&m_inputHandler, &InputHandler::pointerMapped, this, [this](const QPoint point) {
        m_lastQmpPointer = point;
        refreshInputDiagnostics();
    });
    connect(&m_inputHandler, &InputHandler::guestFocusChanged, this, [this](const bool focused) {
        m_guestInputFocused = focused;
        refreshInputDiagnostics();
    });

    createToolbar();

    m_statusLabel = new QLabel(QStringLiteral("Sabi • Starting"), this);
    statusBar()->setObjectName(QStringLiteral("sabiStatusBar"));
    statusBar()->addPermanentWidget(m_statusLabel);
    setStatusText(QStringLiteral("Starting"));
    setAdbActionsEnabled(false);

    m_windowSearchTimer.setInterval(500);
    m_windowSearchTimeout.setSingleShot(true);
    m_closeWaitTimer.setSingleShot(true);
    m_performanceSampleTimer.setInterval(5000);

    connect(&m_qemuController, &QemuController::vmStarted,
            this, &MainWindow::beginDisplayWindowSearch);
    connect(&m_qemuController, &QemuController::vmStarted,
            this, [this]() {
                const QString name = m_instanceName.isEmpty() ? QStringLiteral("default") : m_instanceName;
                m_performanceMonitor.attachProcess(m_qemuController.processId());
                m_performanceMonitor.markQmpReady();
                m_performanceSampleTimer.start();
                if (m_recovery) {
                    QString errorMessage;
                    if (!m_recovery->beginSession(name, m_qemuController.processId(), &errorMessage)) {
                        qWarning().noquote() << errorMessage;
                    }
                }
            });
    connect(&m_qemuController, &QemuController::vmStarted,
            this, [this]() {
                setStatusText(QStringLiteral("Connecting to Android"));
                m_adbBridge.connectToGuest(QStringLiteral("localhost"), m_qemuController.adbHostPort());
            });
    connect(&m_qemuController, &QemuController::vmStopped,
            this, [this](const int exitCode) {
                m_performanceSampleTimer.stop();
                m_performanceMonitor.finish(exitCode);
                if (m_recovery) {
                    const QString name = m_instanceName.isEmpty() ? QStringLiteral("default") : m_instanceName;
                    QString errorMessage;
                    // Non-zero exits leave the marker so the next start can explain
                    // that the last VM process was not known to stop cleanly.
                    if ((exitCode == 0 || m_closePending)
                        && !m_recovery->completeSession(name, &errorMessage)) {
                        qWarning().noquote() << errorMessage;
                    }
                }
                if (m_instanceManager) m_instanceManager->releaseInstance(m_instanceName);
                m_windowSearchTimer.stop();
                m_windowSearchTimeout.stop();
                m_adbBridge.disconnectFromGuest();
                setAdbActionsEnabled(false);
                m_inputHandler.releaseAllInput();
                m_displayEmbed->clearEmbeddedWindow();
                setStatusText(QStringLiteral("Stopped"));
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
                setStatusText(QStringLiteral("Connected (%1), booting...").arg(serial));
            });
    connect(&m_adbBridge, &AdbBridge::authorizationRequired, this,
            [this](const QString& serial) {
                Q_UNUSED(serial)
                setAdbActionsEnabled(false);
                setStatusText(QStringLiteral("Allow USB debugging in Android"), true);
            });
    connect(&m_adbBridge, &AdbBridge::disconnected, this, [this]() {
        setAdbActionsEnabled(false);
        if (m_qemuController.isRunning()) {
            setStatusText(QStringLiteral("Reconnecting to Android"), true);
        }
    });
    connect(&m_adbBridge, &AdbBridge::bootCompleted, this, [this]() {
        m_performanceMonitor.markAndroidReady();
        applyLiveDisplaySettings();
        setStatusText(QStringLiteral("Ready"));
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
                    setStatusText(QStringLiteral("Running (display not embedded)"), true);
                    QMessageBox::warning(
                        this,
                        QStringLiteral("QEMU display not found"),
                        QStringLiteral("QEMU is running, but Sabi could not find its SDL window within 30 seconds. "
                                       "Check that QEMU is using its SDL display backend and the same Windows privilege level."));
                }
            });
    connect(&m_closeWaitTimer, &QTimer::timeout,
            this, &MainWindow::handleCloseWaitTimeout);
    connect(&m_performanceSampleTimer, &QTimer::timeout,
            this, [this] { m_performanceMonitor.sample(); });

    QTimer::singleShot(0, this, &MainWindow::startVm);
}

MainWindow::MainWindow(InstanceManager* manager, const QString& name,
                       RuntimeRecovery* recovery, QWidget* parent)
    : MainWindow(parent)
{
    m_instanceManager = manager; m_instanceName = name; m_recovery = recovery;
    setWindowTitle("Sabi - " + name);
    if (m_instanceLabel) m_instanceLabel->setText(name);
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

bool MainWindow::togglePauseResume()
{
    if (!m_qemuController.isRunning()) return false;
    const bool paused = isVmPaused();
    const QString request = paused ? m_qemuController.resume() : m_qemuController.pause();
    if (request.isEmpty()) return false;
    setStatusText(paused ? QStringLiteral("Resuming") : QStringLiteral("Pausing"));
    return true;
}

bool MainWindow::isVmPaused() const
{
    return m_qemuController.getStatus() == QStringLiteral("paused");
}

void MainWindow::createContextBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("sabiContextBar"));
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(9);

    auto* mark = new QLabel(bar);
    mark->setPixmap(IconFactory::icon(SabiIcon::Mark).pixmap(28, 28));
    mark->setAccessibleName(QStringLiteral("Sabi"));
    auto* wordmark = new QLabel(QStringLiteral("SABI"), bar);
    wordmark->setObjectName(QStringLiteral("sabiWordmark"));
    m_instanceLabel = new QLabel(QStringLiteral("default"), bar);
    m_instanceLabel->setObjectName(QStringLiteral("sabiInstanceName"));
    m_instanceLabel->setAccessibleName(QStringLiteral("Active instance"));
    m_contextStatusIcon = new QLabel(bar);
    m_contextStatusIcon->setAccessibleName(QStringLiteral("Android status"));
    m_contextStatusLabel = new QLabel(bar);
    m_contextStatusLabel->setObjectName(QStringLiteral("sabiContextStatus"));

    layout->addWidget(mark);
    layout->addWidget(wordmark);
    layout->addSpacing(8);
    layout->addWidget(m_instanceLabel);
    layout->addStretch(1);
    layout->addWidget(m_contextStatusIcon);
    layout->addWidget(m_contextStatusLabel);
    // The central shell is built immediately before this method is called.
    auto* shell = findChild<QWidget*>(QStringLiteral("sabiShell"));
    if (shell && shell->layout()) {
        shell->layout()->addWidget(bar);
    }
}

void MainWindow::setStatusText(const QString& text, const bool warning)
{
    const bool ready = text == QStringLiteral("Ready");
    if (m_statusLabel) m_statusLabel->setText(QStringLiteral("Status: %1").arg(text));
    if (m_contextStatusLabel) m_contextStatusLabel->setText(text);
    if (m_contextStatusIcon) {
        const SabiIcon icon = (ready || !warning) ? SabiIcon::StatusReady : SabiIcon::StatusWarning;
        m_contextStatusIcon->setPixmap(IconFactory::icon(icon).pixmap(16, 16));
    }
}

void MainWindow::updateDisplayProfileUi(const QString& profile)
{
    for (auto it = m_displayProfileActions.cbegin(); it != m_displayProfileActions.cend(); ++it) {
        it.value()->setChecked(it.key() == profile);
    }
    if (!m_displayProfileButton) return;
    const SabiIcon icon = profile == QStringLiteral("tablet") ? SabiIcon::Tablet
        : profile == QStringLiteral("pc") || profile == QStringLiteral("fullscreen") ? SabiIcon::Desktop
        : SabiIcon::Phone;
    m_displayProfileButton->setIcon(IconFactory::icon(icon));
    m_displayProfileButton->setToolTip(QStringLiteral("Display profile: %1").arg(profile));
}

void MainWindow::refreshInputDiagnostics()
{
    if (m_inputDiagnosticsOverlay && m_displayEmbed) {
        m_inputDiagnosticsOverlay->setDiagnostics(m_displayEmbed->viewport(),
                                                  m_lastQmpPointer, m_guestInputFocused);
    }
}

void MainWindow::createToolbar()
{
    auto* toolbar = new QToolBar(QStringLiteral("Android controls"), this);
    toolbar->setObjectName(QStringLiteral("sabiToolRail"));
    toolbar->setAccessibleName(QStringLiteral("Sabi Android controls"));
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(21, 21));
    toolbar->setMinimumWidth(56);

    const auto addKeyAction = [this, toolbar](const QString& label, const int keycode, const SabiIcon icon) {
        QAction* action = toolbar->addAction(IconFactory::icon(icon), label);
        action->setToolTip(label);
        m_adbActions.append(action);
        connect(action, &QAction::triggered, this, [this, keycode] { m_adbBridge.sendKeyEvent(keycode); });
    };

    addKeyAction(QStringLiteral("Home"), 3, SabiIcon::Home);
    addKeyAction(QStringLiteral("Back"), 4, SabiIcon::Back);
    addKeyAction(QStringLiteral("Recents"), 187, SabiIcon::Recents);
    toolbar->addSeparator();
    addKeyAction(QStringLiteral("Volume up"), 24, SabiIcon::VolumeUp);
    addKeyAction(QStringLiteral("Volume down"), 25, SabiIcon::VolumeDown);
    addKeyAction(QStringLiteral("Power"), 26, SabiIcon::Power);
    toolbar->addSeparator();

    QAction* rotateAction = toolbar->addAction(IconFactory::icon(SabiIcon::Rotate), QStringLiteral("Rotate"));
    rotateAction->setToolTip(QStringLiteral("Rotate display"));
    connect(rotateAction, &QAction::triggered, this, [this] {
        if (!m_instanceManager || m_instanceName.isEmpty()) return;
        auto config = m_instanceManager->getConfig(m_instanceName);
        auto display = config.value("display").toObject();
        const int width = display.value("width").toInt();
        const int height = display.value("height").toInt();
        if (width < 1 || height < 1) return;
        display["width"] = height; display["height"] = width; display["profile"] = QStringLiteral("custom");
        config["display"] = display;
        if (!m_instanceManager->setConfig(m_instanceName, config)) {
            QMessageBox::warning(this, QStringLiteral("Rotation not saved"), m_instanceManager->lastError());
            return;
        }
        m_launchConfig = config;
        updateDisplayProfileUi(QStringLiteral("custom"));
        applyLiveDisplaySettings();
    });
    QAction* screenshotAction = toolbar->addAction(IconFactory::icon(SabiIcon::Screenshot), QStringLiteral("Screenshot"));
    screenshotAction->setToolTip(QStringLiteral("Save Android screenshot"));
    m_adbActions.append(screenshotAction);
    connect(screenshotAction, &QAction::triggered, this, &MainWindow::saveScreenshot);
    QAction* installAction = toolbar->addAction(IconFactory::icon(SabiIcon::InstallApk), QStringLiteral("Install APK"));
    installAction->setToolTip(QStringLiteral("Install APK"));
    m_adbActions.append(installAction);
    connect(installAction, &QAction::triggered, this, &MainWindow::installSelectedApk);
    toolbar->addSeparator();

    m_fullscreenAction = toolbar->addAction(IconFactory::icon(SabiIcon::Fullscreen), QStringLiteral("Fullscreen"));
    m_fullscreenAction->setToolTip(QStringLiteral("Toggle fullscreen"));
    m_fullscreenAction->setCheckable(true);
    connect(m_fullscreenAction, &QAction::triggered, this, [this](const bool enabled) {
        if (enabled) showFullScreen(); else showNormal();
    });

    QAction* diagnosticsAction = toolbar->addAction(
        IconFactory::icon(SabiIcon::InputDiagnostics), QStringLiteral("Input diagnostics"));
    diagnosticsAction->setToolTip(QStringLiteral("Toggle input diagnostics (Ctrl+Shift+I)"));
    diagnosticsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    diagnosticsAction->setCheckable(true);
    connect(diagnosticsAction, &QAction::toggled, this, [this](const bool enabled) {
        if (!m_inputDiagnosticsOverlay) return;
        m_inputDiagnosticsOverlay->setVisible(enabled);
        if (enabled) refreshInputDiagnostics();
    });

    QAction* runtimeReportAction = toolbar->addAction(
        IconFactory::icon(SabiIcon::InputDiagnostics), QStringLiteral("Save runtime report"));
    runtimeReportAction->setToolTip(QStringLiteral("Save a local, redacted runtime measurement"));
    connect(runtimeReportAction, &QAction::triggered, this, &MainWindow::saveRuntimeReport);

    QAction* controlsAction = toolbar->addAction(IconFactory::icon(SabiIcon::ControlsStudio), QStringLiteral("Controls Studio"));
    controlsAction->setToolTip(QStringLiteral("Create and edit local keyboard mappings"));
    controlsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    connect(controlsAction, &QAction::triggered, this, &MainWindow::openControlsStudio);

    toolbar->addSeparator();
    auto* profiles = new QMenu(toolbar);
    auto* profileGroup = new QActionGroup(profiles);
    profileGroup->setExclusive(true);
    const auto addProfile = [this, profiles, profileGroup](const QString& label, const QString& profile, const SabiIcon icon) {
        QAction* action = profiles->addAction(IconFactory::icon(icon), label);
        action->setCheckable(true);
        action->setData(profile);
        action->setToolTip(QStringLiteral("Apply %1 display profile").arg(label));
        profileGroup->addAction(action);
        m_displayProfileActions.insert(profile, action);
        connect(action, &QAction::triggered, this, [this, profile] { applyDisplayProfile(profile); });
    };
    addProfile(QStringLiteral("Phone"), QStringLiteral("phone"), SabiIcon::Phone);
    addProfile(QStringLiteral("Tablet"), QStringLiteral("tablet"), SabiIcon::Tablet);
    addProfile(QStringLiteral("Desktop"), QStringLiteral("pc"), SabiIcon::Desktop);
    addProfile(QStringLiteral("Fullscreen display"), QStringLiteral("fullscreen"), SabiIcon::Desktop);
    m_displayProfileButton = new QToolButton(toolbar);
    m_displayProfileButton->setObjectName(QStringLiteral("sabiProfileButton"));
    m_displayProfileButton->setMenu(profiles);
    m_displayProfileButton->setPopupMode(QToolButton::InstantPopup);
    m_displayProfileButton->setAccessibleName(QStringLiteral("Display profile"));
    toolbar->addWidget(m_displayProfileButton);
    updateDisplayProfileUi(QStringLiteral("phone"));

    toolbar->addSeparator();
    QAction* settings = toolbar->addAction(IconFactory::icon(SabiIcon::Settings), QStringLiteral("Settings"));
    settings->setToolTip(QStringLiteral("Instance settings"));
    connect(settings, &QAction::triggered, this, [this] {
        if (!m_instanceManager) return;
        SettingsDialog dialog(m_instanceManager->getConfig(m_instanceName), m_instanceManager->isRunning(m_instanceName), this);
        while (dialog.exec() == QDialog::Accepted) {
            if (m_instanceManager->setConfig(m_instanceName, dialog.getConfig())) {
                m_launchConfig = dialog.getConfig();
                updateDisplayProfileUi(m_launchConfig.value("display").toObject().value("profile").toString("custom"));
                applyLiveDisplaySettings();
                break;
            }
            QMessageBox::warning(this, QStringLiteral("Settings not saved"), m_instanceManager->lastError());
        }
    });
    QAction* instancesAction = toolbar->addAction(IconFactory::icon(SabiIcon::Instances), QStringLiteral("Instances"));
    instancesAction->setToolTip(QStringLiteral("Manage instances"));
    connect(instancesAction, &QAction::triggered, this, &MainWindow::instanceListRequested);
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
        updateDisplayProfileUi(m_launchConfig.value("display").toObject().value("profile").toString("custom"));
    }
    m_performanceMonitor.beginLaunch(m_instanceName.isEmpty() ? QStringLiteral("default") : m_instanceName,
                                     m_launchConfig.value(QStringLiteral("overlayImage")).toString());
    const QString savedKeymap = m_launchConfig.value(QStringLiteral("keymapProfile")).toString();
    if (!savedKeymap.isEmpty() && QFileInfo::exists(savedKeymap)) {
        QString errorMessage;
        if (!m_inputHandler.loadKeymapProfile(savedKeymap, &errorMessage)) {
            qWarning().noquote() << "Could not load this instance's keymap:" << errorMessage;
        }
    }
    if (configPath.isEmpty()) {
        setStatusText(QStringLiteral("Configuration missing"), true);
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

    setStatusText(QStringLiteral("Starting VM"));
    if (!m_qemuController.start(configPath)) {
        if (m_instanceManager) m_instanceManager->releaseInstance(m_instanceName);
        setStatusText(QStringLiteral("VM failed to start"), true);
    }
}

void MainWindow::openControlsStudio()
{
    if (m_keymapStudio) {
        m_keymapStudio->raise();
        m_keymapStudio->activateWindow();
        return;
    }
    const auto activeProfile = m_inputHandler.activeKeymapProfile();
    if (!activeProfile) {
        QMessageBox::warning(this, QStringLiteral("Controls Studio unavailable"),
                             QStringLiteral("Sabi could not load a keymap profile. Restart Sabi and try again."));
        return;
    }
    const QString storedPath = m_launchConfig.value(QStringLiteral("keymapProfile")).toString();
    const QString editablePath = QFileInfo::exists(storedPath) ? storedPath : QString{};
    auto* studio = new KeymapStudioDialog(*activeProfile, editablePath, this);
    m_keymapStudio = studio;
    connect(studio, &KeymapStudioDialog::workingProfileChanged, m_keymapOverlay, &KeymapOverlay::setProfile);
    connect(studio, &KeymapStudioDialog::placementModeChanged, this, [this](const bool enabled) {
        m_inputHandler.setKeymapEditing(enabled);
        m_keymapOverlay->setEditing(enabled);
    });
    connect(studio, &KeymapStudioDialog::placementRequested, this, [this, studio] {
        m_keymapOverlay->setProfile(studio->workingProfile());
        m_keymapOverlay->syncToViewport();
    });
    connect(m_keymapOverlay, &KeymapOverlay::placementRequested, studio, &KeymapStudioDialog::applyPlacement);
    connect(studio, &KeymapStudioDialog::profileSaved, this, [this](const QString& path) {
        QString errorMessage;
        if (!m_inputHandler.loadKeymapProfile(path, &errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("Keymap not loaded"), errorMessage);
            return;
        }
        m_inputHandler.setKeymapEnabled(true);
        m_launchConfig[QStringLiteral("keymapProfile")] = path;
        if (m_instanceManager && !m_instanceName.isEmpty()
            && !m_instanceManager->setConfig(m_instanceName, m_launchConfig)) {
            QMessageBox::warning(this, QStringLiteral("Keymap saved locally"),
                                 QStringLiteral("The keymap was saved, but could not be selected for this instance: %1")
                                     .arg(m_instanceManager->lastError()));
        }
    });
    connect(studio, &QObject::destroyed, this, [this] {
        m_inputHandler.setKeymapEditing(false);
        if (m_keymapOverlay) m_keymapOverlay->setEditing(false);
    });
    m_keymapOverlay->setProfile(*activeProfile);
    studio->show();
    studio->raise();
    studio->activateWindow();
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
    updateDisplayProfileUi(profile);
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
    setStatusText(QStringLiteral("Running (locating display)"));
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
        setStatusText(QStringLiteral("Running (display not embedded)"), true);
        QMessageBox::warning(this,
                             QStringLiteral("Could not embed QEMU display"),
                             errorMessage);
        return;
    }

    m_windowSearchTimer.stop();
    m_windowSearchTimeout.stop();
    setStatusText(QStringLiteral("Running"));
}

void MainWindow::handleVmError(const QString& message)
{
    setStatusText(QStringLiteral("VM error"), true);
    QMessageBox::critical(this, QStringLiteral("Sabi VM error"), message);
}

void MainWindow::handleAdbError(const QString& message)
{
    qWarning().noquote() << message;
    if (!m_adbBridge.isBootCompleted()) {
        setStatusText(QStringLiteral("Android unavailable"), true);
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

void MainWindow::saveRuntimeReport()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath();
    }
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save local runtime report"),
        QDir(directory).filePath(QStringLiteral("sabi-runtime-report.txt")),
        QStringLiteral("Text files (*.txt)"));
    if (path.isEmpty()) {
        return;
    }
    QString errorMessage;
    if (!m_performanceMonitor.saveRedactedReport(path, &errorMessage)) {
        QMessageBox::warning(this, QStringLiteral("Runtime report not saved"), errorMessage);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Local runtime report saved"), 6000);
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
        setStatusText(QStringLiteral("Shutting down"));
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
    setStatusText(QStringLiteral("Shutting down"));
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
