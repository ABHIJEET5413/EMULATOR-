#pragma once

#include <QList>
#include <QMainWindow>
#include <QTimer>
#include <QString>

#include "adb_bridge.h"
#include "input_handler.h"
#include "qemu_controller.h"

class QAction;
class QComboBox;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class DisplayEmbed;
class InstanceManager;

/**
 * @file main_window.h
 * @brief Qt desktop window, toolbar, guest display, and VM lifecycle UI.
 */
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /** Create the Sabi desktop window and prepare VM control. */
    explicit MainWindow(QWidget* parent = nullptr);
    /** Open a managed instance whose running lock has already been acquired. */
    MainWindow(InstanceManager* manager, const QString& name, QWidget* parent = nullptr);
    /** Stop QEMU before releasing the managed instance lock. */
    ~MainWindow() override;

    /** Queue an APK from the command line for installation after Android boots. */
    void queueApkInstall(const QString& apkPath);

signals:
    /** Request the multi-instance launcher from the toolbar. */
    void instanceListRequested();

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
    /** Size the host window to the selected guest aspect ratio and desktop space. */
    void resizeForGuestDisplay(QSize resolution);
    /** Save and apply a profile change for the active instance. */
    void applyDisplayProfile(const QString& profile);
    /** Push display dimensions to Android when ADB is ready. */
    void applyLiveDisplaySettings();
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
    QComboBox* m_displayProfile{nullptr};
    QList<QAction*> m_adbActions;
    QString m_pendingApkPath;
    QTimer m_windowSearchTimer;
    QTimer m_windowSearchTimeout;
    QTimer m_closeWaitTimer;
    bool m_closePending{false};
    bool m_allowClose{false};
    InstanceManager* m_instanceManager{nullptr};
    QString m_instanceName;
    QJsonObject m_launchConfig;
};
