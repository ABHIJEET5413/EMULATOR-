#include <QLabel>
#include <QVBoxLayout>

#include "input_diagnostics_overlay.h"

InputDiagnosticsOverlay::InputDiagnosticsOverlay(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("sabiInputDiagnostics"));
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame#sabiInputDiagnostics { background: rgba(10, 15, 24, 220); border: 1px solid #6ea8fe; border-radius: 7px; }"
        "QLabel { color: #dce8ff; font-family: Consolas, monospace; font-size: 11px; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(9, 7, 9, 7);
    m_text = new QLabel(this);
    m_text->setAccessibleName(QStringLiteral("Input diagnostics"));
    layout->addWidget(m_text);
    setFixedWidth(290);
    hide();
}

void InputDiagnosticsOverlay::setDiagnostics(const GuestViewport& viewport,
                                             const QPoint lastQmpPoint,
                                             const bool guestFocused)
{
    const QRect logical = viewport.logicalContentRect();
    const QRect physical = viewport.physicalContentRect();
    m_text->setText(QStringLiteral("INPUT DIAGNOSTICS\n"
                                   "logical: %1,%2  %3x%4\n"
                                   "physical: %5,%6  %7x%8\n"
                                   "last QMP: %9, %10\n"
                                   "guest focus: %11")
                        .arg(logical.x()).arg(logical.y()).arg(logical.width()).arg(logical.height())
                        .arg(physical.x()).arg(physical.y()).arg(physical.width()).arg(physical.height())
                        .arg(lastQmpPoint.x()).arg(lastQmpPoint.y())
                        .arg(guestFocused ? QStringLiteral("yes") : QStringLiteral("no")));
    adjustSize();
    move(12, 12);
    raise();
}
