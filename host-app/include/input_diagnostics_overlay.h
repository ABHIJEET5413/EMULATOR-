#pragma once

#include <QFrame>
#include <QPoint>

#include "guest_viewport.h"

class QLabel;

/**
 * @file input_diagnostics_overlay.h
 * @brief Renders an optional transparent-to-input local viewport diagnostic panel.
 */
class InputDiagnosticsOverlay final : public QFrame {
    Q_OBJECT

public:
    /** Create a non-interactive diagnostic panel over the display host widget. */
    explicit InputDiagnosticsOverlay(QWidget* parent = nullptr);

    /** Update displayed geometry, focus state, and most recent QMP coordinate. */
    void setDiagnostics(const GuestViewport& viewport, QPoint lastQmpPoint, bool guestFocused);

private:
    QLabel* m_text{nullptr};
};
