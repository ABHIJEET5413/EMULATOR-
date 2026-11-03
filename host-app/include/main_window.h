#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QString>

#include "adb_bridge.h"
#include "input_handler.h"
#include "performance_monitor.h"
#include "qemu_controller.h"

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QToolButton;
class DisplayEmbed;
class InputDiagnosticsOverlay;
class InstanceManager;
class KeymapOverlay;
class KeymapStudioDialog;
class RuntimeRecovery;

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
    MainWindow(InstanceManager* manager, const QString& name,
               RuntimeRecovery* recovery = nullptr, QWidget* parent = nullptr);
    /** Stop QEMU before releasing the managed instance lock. */
    ~MainWindow() override;

    /** Queue an APK from the command line for installation after Android boots. */
    void queueApkInstall(const QString& apkPath);
    /** Pause or resume this window's own QEMU process through QMP. */
    bool togglePauseResume();
    /** Return whether this window's own QEMU process is paused. */
    bool isVmPaused() const;

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
    void createContextBar();
    void createToolbar();
    void setStatusText(const QString& text, bool warning = false);
    void updateDisplayProfileUi(const QString& profile);
    void refreshInputDiagnostics();
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
    /** Open the modeless per-instance keymap editor. */
    void openControlsStudio();
    /** Export a user-selected local report for the most recent VM session. */
    void saveRuntimeReport();

    QemuController m_qemuController;
    AdbBridge m_adbBridge;
    InputHandler m_inputHandler;
    DisplayEmbed* m_displayEmbed{nullptr};
    InputDiagnosticsOverlay* m_inputDiagnosticsOverlay{nullptr};
    KeymapOverlay* m_keymapOverlay{nullptr};
    QPointer<KeymapStudioDialog> m_keymapStudio;
    QLabel* m_statusLabel{nullptr};
    QLabel* m_contextStatusLabel{nullptr};
    QLabel* m_contextStatusIcon{nullptr};
    QLabel* m_instanceLabel{nullptr};
    QAction* m_fullscreenAction{nullptr};
    QToolButton* m_displayProfileButton{nullptr};
    QHash<QString, QAction*> m_displayProfileActions;
    QList<QAction*> m_adbActions;
    QString m_pendingApkPath;
    QPoint m_lastQmpPointer{-1, -1};
    bool m_guestInputFocused{false};
    QTimer m_windowSearchTimer;
    QTimer m_windowSearchTimeout;
    QTimer m_closeWaitTimer;
    QTimer m_performanceSampleTimer;
    bool m_closePending{false};
    bool m_allowClose{false};
    InstanceManager* m_instanceManager{nullptr};
    RuntimeRecovery* m_recovery{nullptr};
    PerformanceMonitor m_performanceMonitor;
    QString m_instanceName;
    QJsonObject m_launchConfig;
};
