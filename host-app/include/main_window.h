#pragma once

#include <QList>
#include <QMainWindow>
#include <QTimer>
#include <QString>

#include "adb_bridge.h"
#include "input_handler.h"
#include "qemu_controller.h"

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class DisplayEmbed;

/**
 * @file main_window.h
 * @brief Qt desktop window, toolbar, guest display, and VM lifecycle UI.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /** Create the DroidBox desktop window and prepare Phase 1 VM control. */
    explicit MainWindow(QWidget* parent = nullptr);

    /** Queue an APK from the command line for installation after Android boots. */
    void queueApkInstall(const QString& apkPath);

protected:
    /** Shut down QEMU before allowing the application window to close. */
    void closeEvent(QCloseEvent* event) override;

    /** Keep the fullscreen toolbar action synchronized with window state changes. */
    void changeEvent(QEvent* event) override;

    /** Accept local files for ADB transfer after the guest is ready. */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /** Install dropped APKs or copy other files into Android Downloads. */
    void dropEvent(QDropEvent* event) override;

private:
    void createToolbar();
    QString findDefaultConfig() const;
    void startVm();
    void beginDisplayWindowSearch();
    void searchForQemuWindow();
    void handleVmError(const QString& message);
    void handleAdbError(const QString& message);
    void handleCloseWaitTimeout();
    void updateFullscreenAction();
    void setAdbActionsEnabled(bool enabled);
    void installSelectedApk();
    void saveScreenshot();
    void installPendingApk();

    QemuController m_qemuController;
    AdbBridge m_adbBridge;
    InputHandler m_inputHandler;
    DisplayEmbed* m_displayEmbed{nullptr};
    QLabel* m_statusLabel{nullptr};
    QAction* m_fullscreenAction{nullptr};
    QList<QAction*> m_adbActions;
    QString m_pendingApkPath;
    QTimer m_windowSearchTimer;
    QTimer m_windowSearchTimeout;
    QTimer m_closeWaitTimer;
    bool m_closePending{false};
    bool m_allowClose{false};
};
