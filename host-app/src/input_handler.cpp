#include <QEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QWidget>

#include "input_handler.h"
#include "display_embed.h"
#include "qemu_controller.h"

namespace {

QJsonObject absoluteAxisEvent(const QString& axis, const int value)
{
    return {{QStringLiteral("type"), QStringLiteral("abs")},
            {QStringLiteral("data"),
             QJsonObject{{QStringLiteral("axis"), axis},
                         {QStringLiteral("value"), value}}}};
}

QString qmpButtonName(const Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return QStringLiteral("left");
    case Qt::MiddleButton:
        return QStringLiteral("middle");
    case Qt::RightButton:
        return QStringLiteral("right");
    default:
        return {};
    }
}

QJsonObject buttonEvent(const QString& button, const bool down)
{
    return {{QStringLiteral("type"), QStringLiteral("btn")},
            {QStringLiteral("data"),
             QJsonObject{{QStringLiteral("button"), button},
                         {QStringLiteral("down"), down}}}};
}

QJsonObject keyEvent(const int keyCode, const bool down)
{
    return {{QStringLiteral("type"), QStringLiteral("key")},
            {QStringLiteral("data"),
             QJsonObject{{QStringLiteral("key"),
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                      {QStringLiteral("data"), keyCode}}},
                         {QStringLiteral("down"), down}}}};
}

} // namespace

InputHandler::InputHandler(QObject* parent)
    : QObject(parent)
{
}

void InputHandler::installOn(QWidget* displayWidget, QemuController* controller)
{
    if (m_displayWidget) {
        m_displayWidget->removeEventFilter(this);
    }

    m_displayWidget = displayWidget;
    m_controller = controller;
    if (m_displayWidget) {
        m_displayWidget->installEventFilter(this);
        m_displayWidget->setFocusPolicy(Qt::StrongFocus);
    }
}

int InputHandler::scaleToQmpAxis(const int coordinate, const int extent)
{
    if (extent <= 1) {
        return 0;
    }

    const int boundedCoordinate = qBound(0, coordinate, extent - 1);
    return qRound(static_cast<qreal>(boundedCoordinate) * 32767.0 / (extent - 1));
}

int InputHandler::keyCodeForQtKey(const int qtKey)
{
    // QMP numeric KeyValue values use Linux input keycodes (Enter is 28).
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        static constexpr int letterCodes[] = {
            30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
            49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44
        };
        return letterCodes[qtKey - Qt::Key_A];
    }
    if (qtKey >= Qt::Key_1 && qtKey <= Qt::Key_9) {
        return 2 + (qtKey - Qt::Key_1);
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F10) {
        return 59 + (qtKey - Qt::Key_F1);
    }

    switch (qtKey) {
    case Qt::Key_Escape: return 1;
    case Qt::Key_0: return 11;
    case Qt::Key_Minus: return 12;
    case Qt::Key_Equal: return 13;
    case Qt::Key_Backspace: return 14;
    case Qt::Key_Tab: return 15;
    case Qt::Key_BracketLeft: return 26;
    case Qt::Key_BracketRight: return 27;
    case Qt::Key_Return:
    case Qt::Key_Enter: return 28;
    case Qt::Key_Control: return 29;
    case Qt::Key_Semicolon: return 39;
    case Qt::Key_Apostrophe: return 40;
    case Qt::Key_QuoteLeft: return 41;
    case Qt::Key_Shift: return 42;
    case Qt::Key_Backslash: return 43;
    case Qt::Key_Comma: return 51;
    case Qt::Key_Period: return 52;
    case Qt::Key_Slash: return 53;
    case Qt::Key_Alt: return 56;
    case Qt::Key_Space: return 57;
    case Qt::Key_CapsLock: return 58;
    case Qt::Key_F11: return 87;
    case Qt::Key_F12: return 88;
    case Qt::Key_Print: return 99;
    case Qt::Key_Home: return 102;
    case Qt::Key_Up: return 103;
    case Qt::Key_PageUp: return 104;
    case Qt::Key_Left: return 105;
    case Qt::Key_Right: return 106;
    case Qt::Key_End: return 107;
    case Qt::Key_Down: return 108;
    case Qt::Key_PageDown: return 109;
    case Qt::Key_Insert: return 110;
    case Qt::Key_Delete: return 111;
    case Qt::Key_Pause: return 119;
    case Qt::Key_Meta: return 125;
    case Qt::Key_Menu: return 127;
    default: return -1;
    }
}

bool InputHandler::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_displayWidget || !m_displayWidget) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::FocusIn:
        if (auto* display = qobject_cast<DisplayEmbed*>(m_displayWidget.data())) {
            display->focusEmbeddedWindow();
        }
        return false;
    case QEvent::FocusOut:
        releaseHeldInput();
        return false;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        return handleMouseEvent(static_cast<QMouseEvent*>(event));
    case QEvent::KeyPress:
        return handleKeyEvent(static_cast<QKeyEvent*>(event), true);
    case QEvent::KeyRelease:
        return handleKeyEvent(static_cast<QKeyEvent*>(event), false);
    default:
        return QObject::eventFilter(watched, event);
    }
}

bool InputHandler::handleMouseEvent(QMouseEvent* event)
{
    if (!event || !m_displayWidget || !m_controller) {
        return false;
    }

    const QPoint position = event->position().toPoint();
    QJsonArray events{
        absoluteAxisEvent(QStringLiteral("x"), scaleToQmpAxis(position.x(), m_displayWidget->width())),
        absoluteAxisEvent(QStringLiteral("y"), scaleToQmpAxis(position.y(), m_displayWidget->height()))
    };

    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonRelease) {
        const QString button = qmpButtonName(event->button());
        if (button.isEmpty()) {
            return false;
        }
        const bool down = event->type() == QEvent::MouseButtonPress;
        events.append(buttonEvent(button, down));
        if (down) {
            m_pressedButtons.insert(static_cast<int>(event->button()));
        } else {
            m_pressedButtons.remove(static_cast<int>(event->button()));
        }
    }

    return sendInputEvents(events);
}

bool InputHandler::handleKeyEvent(QKeyEvent* event, const bool down)
{
    if (!event || !m_controller || event->isAutoRepeat()) {
        return false;
    }

    const int keyCode = keyCodeForQtKey(event->key());
    if (keyCode < 0) {
        return false;
    }

    if (down) {
        m_pressedKeys.insert(keyCode);
    } else {
        m_pressedKeys.remove(keyCode);
    }
    return sendInputEvents({keyEvent(keyCode, down)});
}

void InputHandler::releaseHeldInput()
{
    QJsonArray events;
    for (const int buttonValue : m_pressedButtons) {
        const QString button = qmpButtonName(static_cast<Qt::MouseButton>(buttonValue));
        if (!button.isEmpty()) {
            events.append(buttonEvent(button, false));
        }
    }
    for (const int keyCode : m_pressedKeys) {
        events.append(keyEvent(keyCode, false));
    }
    m_pressedButtons.clear();
    m_pressedKeys.clear();
    sendInputEvents(events);
}

bool InputHandler::sendInputEvents(const QJsonArray& events)
{
    return m_controller && !m_controller->sendInputEvents(events).isEmpty();
}
