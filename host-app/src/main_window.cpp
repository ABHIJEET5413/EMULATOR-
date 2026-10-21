#include <QAction>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QWidget>
#include <QLoggingCategory>

#include "display_embed.h"
#include "main_window.h"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("DroidBox"));
    setMinimumSize(480, 700);
    resize(600, 850);
    setAcceptDrops(true);

    m_displayEmbed = new DisplayEmbed(this);
    setCentralWidget(m_displayEmbed);
    m_inputHandler.installOn(m_displayEmbed, &m_qemuController);

    createToolbar();

    m_statusLabel = new QLabel(QStringLiteral("Status: Starting"), this);
    statusBar()->addPermanentWidget(m_statusLabel);

    m_windowSearchTimer.setInterval(500);
    m_windowSearchTimeout.setSingleShot(true);
    m_closeWaitTimer.setSingleShot(true);

    connect(&m_qemuController, &QemuController::vmStarted,
            this, &MainWindow::beginDisplayWindowSearch);
    connect(&m_qemuController, &QemuController::vmStopped,
            this, [this](const int exitCode) {
                Q_UNUSED(exitCode)
                m_windowSearchTimer.stop();
                m_windowSearchTimeout.stop();
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
                        QStringLiteral("QEMU is running, but DroidBox could not find its SDL window within 30 seconds. "
                                       "Check that QEMU is using its SDL display backend and the same Windows privilege level."));
                }
            });
    connect(&m_closeWaitTimer, &QTimer::timeout,
            this, &MainWindow::handleCloseWaitTimeout);

    QTimer::singleShot(0, this, &MainWindow::startVm);
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

    addStub(QStringLiteral("Home"));
    addStub(QStringLiteral("Back"));
    addStub(QStringLiteral("Recents"));
    toolbar->addSeparator();
    addStub(QStringLiteral("Volume +"));
    addStub(QStringLiteral("Volume -"));
    toolbar->addSeparator();
    addStub(QStringLiteral("Rotate"));
    addStub(QStringLiteral("Screenshot"));
    addStub(QStringLiteral("Install APK"));
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
    addStub(QStringLiteral("Shared Folder"));
    addStub(QStringLiteral("Settings"));
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
    const QString configPath = findDefaultConfig();
    if (configPath.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Status: Configuration missing"));
        QMessageBox::critical(
            this,
            QStringLiteral("VM configuration not found"),
            QStringLiteral("Could not find vm/configs/default-instance.json. "
                           "Run DroidBox from the project folder or place the vm folder next to the application."));
        return;
    }

    m_statusLabel->setText(QStringLiteral("Status: Starting VM"));
    if (!m_qemuController.start(configPath)) {
        m_statusLabel->setText(QStringLiteral("Status: VM failed to start"));
    }
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
    QMessageBox::critical(this, QStringLiteral("DroidBox VM error"), message);
}

void MainWindow::handleCloseWaitTimeout()
{
    if (!m_closePending || !m_qemuController.isRunning()) {
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        QStringLiteral("DroidBox"),
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
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty()) {
            qInfo().noquote() << "File dropped:" << path << "(file handling is not connected yet).";
        }
    }
    event->acceptProposedAction();
}
