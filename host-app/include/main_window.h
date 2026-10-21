#pragma once

#include <QMainWindow>
#include <QTimer>

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

protected:
    /** Shut down QEMU before allowing the application window to close. */
    void closeEvent(QCloseEvent* event) override;

    /** Keep the fullscreen toolbar action synchronized with window state changes. */
    void changeEvent(QEvent* event) override;

    /** Accept files so Phase 2 can log drops for later phases. */
    void dragEnterEvent(QDragEnterEvent* event) override;

    /** Log dropped file paths without installing or copying them yet. */
    void dropEvent(QDropEvent* event) override;

private:
    void createToolbar();
    QString findDefaultConfig() const;
    void startVm();
    void beginDisplayWindowSearch();
    void searchForQemuWindow();
    void handleVmError(const QString& message);
    void handleCloseWaitTimeout();
    void updateFullscreenAction();

    QemuController m_qemuController;
    InputHandler m_inputHandler;
    DisplayEmbed* m_displayEmbed{nullptr};
    QLabel* m_statusLabel{nullptr};
    QAction* m_fullscreenAction{nullptr};
    QTimer m_windowSearchTimer;
    QTimer m_windowSearchTimeout;
    QTimer m_closeWaitTimer;
    bool m_closePending{false};
    bool m_allowClose{false};
};
