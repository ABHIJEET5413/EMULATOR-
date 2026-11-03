#include <QMouseEvent>
#include <QPainter>
#include <QKeySequence>

#include "display_embed.h"
#include "keymap_overlay.h"
#include "ui_theme.h"

KeymapOverlay::KeymapOverlay(DisplayEmbed* display)
    : QWidget(display), m_display(display)
{
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setVisible(false);
    if (m_display) connect(m_display, &DisplayEmbed::viewportChanged, this, &KeymapOverlay::syncToViewport);
}

void KeymapOverlay::setEditing(const bool editing)
{
    m_editing = editing;
    setAttribute(Qt::WA_TransparentForMouseEvents, !editing);
    syncToViewport();
    setVisible(editing);
    if (editing) raise();
}

void KeymapOverlay::setProfile(const KeymapProfile& profile)
{
    m_profile = profile;
    update();
}

void KeymapOverlay::syncToViewport()
{
    if (!m_display) return;
    setGeometry(m_display->viewport().logicalContentRect());
    if (m_editing) { raise(); update(); }
}

void KeymapOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    if (!m_editing || m_profile.resolution.width() < 1 || m_profile.resolution.height() < 1) return;
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(7, 13, 24, 70));
    painter.setPen(QPen(UiTheme::accentColor(), 2)); painter.setBrush(QColor(29, 79, 145, 210));
    const auto map = [this](const int x, const int y) {
        return QPoint(qRound(static_cast<qreal>(x) * width() / m_profile.resolution.width()),
                      qRound(static_cast<qreal>(y) * height() / m_profile.resolution.height()));
    };
    for (auto it = m_profile.bindings.cbegin(); it != m_profile.bindings.cend(); ++it) {
        const KeyAction& action = it.value();
        if (action.type == KeyAction::Passthrough) continue;
        QPoint point = map(action.type == KeyAction::Swipe ? action.startX : action.x,
                           action.type == KeyAction::Swipe ? action.startY : action.y);
        painter.drawEllipse(point, 15, 15);
        painter.drawText(QRect(point.x() - 24, point.y() - 10, 48, 20), Qt::AlignCenter,
                         QKeySequence(it.key()).toString(QKeySequence::NativeText));
        if (action.type == KeyAction::Swipe) painter.drawLine(point, map(action.endX, action.endY));
    }
    painter.setPen(Qt::white);
    painter.drawText(rect().adjusted(12, 10, -12, -10), Qt::AlignTop | Qt::AlignHCenter,
                     QStringLiteral("Controls Studio — click to place the selected action"));
}

void KeymapOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_editing || m_profile.resolution.width() < 1 || m_profile.resolution.height() < 1) return;
    const QPoint point(qBound(0, qRound(static_cast<qreal>(event->position().x()) * m_profile.resolution.width() / qMax(1, width())), m_profile.resolution.width() - 1),
                       qBound(0, qRound(static_cast<qreal>(event->position().y()) * m_profile.resolution.height() / qMax(1, height())), m_profile.resolution.height() - 1));
    emit placementRequested(point);
    event->accept();
}
