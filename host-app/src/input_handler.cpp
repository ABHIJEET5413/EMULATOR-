#include <QEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QList>
#include <QWidget>

#include "input_handler.h"
#include "display_embed.h"
#include "qemu_controller.h"

#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

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

#ifdef Q_OS_WIN
QList<QPointer<InputHandler>> g_keyboardHandlers;
HHOOK g_keyboardHook{nullptr};

Qt::Key keyFromVirtualKey(const UINT virtualKey)
{
    if (virtualKey >= 'A' && virtualKey <= 'Z') {
        return static_cast<Qt::Key>(Qt::Key_A + virtualKey - 'A');
    }
    if (virtualKey >= '0' && virtualKey <= '9') {
        return static_cast<Qt::Key>(Qt::Key_0 + virtualKey - '0');
    }
    if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
        return static_cast<Qt::Key>(Qt::Key_F1 + virtualKey - VK_F1);
    }

    switch (virtualKey) {
    case VK_ESCAPE: return Qt::Key_Escape;
    case VK_RETURN: return Qt::Key_Return;
    case VK_SPACE: return Qt::Key_Space;
    case VK_TAB: return Qt::Key_Tab;
    case VK_BACK: return Qt::Key_Backspace;
    case VK_DELETE: return Qt::Key_Delete;
    case VK_INSERT: return Qt::Key_Insert;
    case VK_HOME: return Qt::Key_Home;
    case VK_END: return Qt::Key_End;
    case VK_PRIOR: return Qt::Key_PageUp;
    case VK_NEXT: return Qt::Key_PageDown;
    case VK_UP: return Qt::Key_Up;
    case VK_DOWN: return Qt::Key_Down;
    case VK_LEFT: return Qt::Key_Left;
    case VK_RIGHT: return Qt::Key_Right;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT: return Qt::Key_Shift;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL: return Qt::Key_Control;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU: return Qt::Key_Alt;
    case VK_CAPITAL: return Qt::Key_CapsLock;
    case VK_SNAPSHOT: return Qt::Key_Print;
    case VK_PAUSE: return Qt::Key_Pause;
    case VK_APPS: return Qt::Key_Menu;
    case VK_LWIN:
    case VK_RWIN: return Qt::Key_Meta;
    case VK_OEM_MINUS: return Qt::Key_Minus;
    case VK_OEM_PLUS: return Qt::Key_Equal;
    case VK_OEM_4: return Qt::Key_BracketLeft;
    case VK_OEM_6: return Qt::Key_BracketRight;
    case VK_OEM_1: return Qt::Key_Semicolon;
    case VK_OEM_7: return Qt::Key_Apostrophe;
    case VK_OEM_3: return Qt::Key_QuoteLeft;
    case VK_OEM_5:
    case VK_OEM_102: return Qt::Key_Backslash;
    case VK_OEM_COMMA: return Qt::Key_Comma;
    case VK_OEM_PERIOD: return Qt::Key_Period;
    case VK_OEM_2: return Qt::Key_Slash;
    default: return Qt::Key_unknown;
    }
}

LRESULT CALLBACK keyboardHookProc(const int code, const WPARAM message, const LPARAM data)
{
    if (code == HC_ACTION
        && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN
            || message == WM_KEYUP || message == WM_SYSKEYUP)) {
        const auto* keyData = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (InputHandler::dispatchNativeKeyEvent(keyData->vkCode, down)) return 1;
    }
    return CallNextHookEx(g_keyboardHook, code, message, data);
}

#endif

} // namespace

InputHandler::InputHandler(QObject* parent)
    : QObject(parent)
{
    connect(&m_keymapEngine, &KeymapEngine::touchDown,
            this, [this](const int x, const int y) {
                sendTouchPosition(x, y, true);
            });
    connect(&m_keymapEngine, &KeymapEngine::touchUp,
            this, [this](const int x, const int y) {
                sendTouchPosition(x, y, false);
            });
    connect(&m_keymapEngine, &KeymapEngine::touchMove,
            this, &InputHandler::sendTouchMove);
    connect(&m_keymapEngine, &KeymapEngine::keyPassthrough,
            this, &InputHandler::sendKeyToGuest);
}

InputHandler::~InputHandler()
{
#ifdef Q_OS_WIN
    g_keyboardHandlers.removeAll(this);
    if (g_keyboardHandlers.isEmpty() && g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
#endif
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

    if (!m_keymapEngine.activeProfile()) {
        const QString defaultProfile = QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("resources/keymaps/default.json"));
        if (QFileInfo::exists(defaultProfile)) {
            QString errorMessage;
            if (!m_keymapEngine.loadProfile(defaultProfile, &errorMessage)) {
                qWarning().noquote() << "Could not load the default keymap profile:" << errorMessage;
            }
        } else {
            qWarning() << "The default keymap profile is missing:" << defaultProfile;
        }
    }

#ifdef Q_OS_WIN
    if (!g_keyboardHandlers.contains(this)) {
        g_keyboardHandlers.append(this);
    }
    if (!g_keyboardHook) {
        g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL,
                                           keyboardHookProc,
                                           GetModuleHandleW(nullptr),
                                           0);
        if (!g_keyboardHook) {
            qWarning() << "Could not install the guest keymap keyboard hook.";
        }
    }
#endif
}

bool InputHandler::loadKeymapProfile(const QString& path, QString* errorMessage)
{
    return m_keymapEngine.loadProfile(path, errorMessage);
}

void InputHandler::setKeymapEnabled(const bool enabled)
{
    m_keymapEngine.setEnabled(enabled);
}

bool InputHandler::isKeymapEnabled() const
{
    return m_keymapEngine.isEnabled();
}

void InputHandler::setGuestResolution(const QSize resolution)
{
    m_keymapEngine.setGuestResolution(resolution);
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
    // QMP number values use XT set-1 codes with bit 7 marking extended keys.
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
    case Qt::Key_Print: return 0xb7;
    case Qt::Key_Home: return 0xc7;
    case Qt::Key_Up: return 0xc8;
    case Qt::Key_PageUp: return 0xc9;
    case Qt::Key_Left: return 0xcb;
    case Qt::Key_Right: return 0xcd;
    case Qt::Key_End: return 0xcf;
    case Qt::Key_Down: return 0xd0;
    case Qt::Key_PageDown: return 0xd1;
    case Qt::Key_Insert: return 0xd2;
    case Qt::Key_Delete: return 0xd3;
    case Qt::Key_Pause: return 0xc6;
    case Qt::Key_Meta: return 0xdb;
    case Qt::Key_Menu: return 0xdd;
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
        m_keymapEngine.releaseAll();
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

#ifdef Q_OS_WIN
    // Depending on the Windows compositor/DPI path, pointer messages over a
    // reparented SDL child can be delivered either to SDL or to the native Qt
    // parent. SDL handles the former itself; replay the latter as the original
    // Win32 mouse message instead of converting physical input into QMP.
    if (forwardNativeMouseEvent(event)) {
        return true;
    }
#endif

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

bool InputHandler::forwardNativeMouseEvent(QMouseEvent* event)
{
#ifdef Q_OS_WIN
    auto* display = qobject_cast<DisplayEmbed*>(m_displayWidget.data());
    if (!event || !display || !display->hasEmbeddedWindow()) {
        return false;
    }
    const HWND guestWindow = reinterpret_cast<HWND>(display->embeddedWindow());
    if (!IsWindow(guestWindow)) {
        return false;
    }

    UINT message = WM_MOUSEMOVE;
    switch (event->type()) {
    case QEvent::MouseMove: break;
    case QEvent::MouseButtonPress:
        if (event->button() == Qt::LeftButton) message = WM_LBUTTONDOWN;
        else if (event->button() == Qt::RightButton) message = WM_RBUTTONDOWN;
        else if (event->button() == Qt::MiddleButton) message = WM_MBUTTONDOWN;
        else return false;
        break;
    case QEvent::MouseButtonRelease:
        if (event->button() == Qt::LeftButton) message = WM_LBUTTONUP;
        else if (event->button() == Qt::RightButton) message = WM_RBUTTONUP;
        else if (event->button() == Qt::MiddleButton) message = WM_MBUTTONUP;
        else return false;
        break;
    default: return false;
    }

    const QPoint global = event->globalPosition().toPoint();
    POINT clientPoint{global.x(), global.y()};
    if (!ScreenToClient(guestWindow, &clientPoint)) {
        return false;
    }
    WPARAM buttons = 0;
    if (event->buttons().testFlag(Qt::LeftButton)) buttons |= MK_LBUTTON;
    if (event->buttons().testFlag(Qt::RightButton)) buttons |= MK_RBUTTON;
    if (event->buttons().testFlag(Qt::MiddleButton)) buttons |= MK_MBUTTON;
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN) {
        display->focusEmbeddedWindow();
        SetCapture(guestWindow);
    }
    const LPARAM position = MAKELPARAM(static_cast<WORD>(clientPoint.x), static_cast<WORD>(clientPoint.y));
    if (!PostMessageW(guestWindow, message, buttons, position)) {
        return false;
    }
    if (message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP) {
        ReleaseCapture();
    }
    return true;
#else
    Q_UNUSED(event)
    return false;
#endif
}

bool InputHandler::handleKeyEvent(QKeyEvent* event, const bool down)
{
    if (!event) {
        return false;
    }

    const auto key = static_cast<Qt::Key>(event->key());
    if (event->isAutoRepeat()) {
        return m_keymapEngine.isEnabled() && m_keymapEngine.handleKeyPress(key);
    }

    if (down) {
        if (m_keymapEngine.handleKeyPress(key)) {
            return true;
        }
    } else if (m_keymapEngine.handleKeyRelease(key)) {
        return true;
    }
    return sendKeyToGuest(key, down);
}

bool InputHandler::sendKeyToGuest(const Qt::Key key, const bool down)
{
    if (!m_controller) {
        return false;
    }
    const int keyCode = keyCodeForQtKey(static_cast<int>(key));
    if (keyCode < 0) {
        return false;
    }

    const bool sent = sendInputEvents({keyEvent(keyCode, down)});
    if (down && sent) {
        m_pressedKeys.insert(keyCode);
    } else if (!down) {
        m_pressedKeys.remove(keyCode);
    }
    return sent;
}

bool InputHandler::handleNativeKeyEvent(const int virtualKey, const bool down)
{
#ifdef Q_OS_WIN
    const Qt::Key key = keyFromVirtualKey(static_cast<UINT>(virtualKey));
    if (key == Qt::Key_unknown) {
        return false;
    }
    if (!down) {
        // A mapped hold must be released even if focus moved after key-down.
        if (m_keymapEngine.handleKeyRelease(key)) {
            return true;
        }
        const int code = keyCodeForQtKey(static_cast<int>(key));
        if (m_pressedKeys.contains(code)) {
            sendKeyToGuest(key, false);
            return true;
        }
        return false;
    }
    if (!isEmbeddedDisplayFocused()) return false;
    if (m_keymapEngine.handleKeyPress(key)) return true;
    // Reparented SDL windows cannot be relied on to receive Windows keyboard
    // messages. Use the focused guest's QMP path even when keymaps are disabled,
    // and consume successful forwards so SDL cannot deliver a second copy.
    if (sendKeyToGuest(key, true)) {
        return true;
    }
    return false;
#else
    Q_UNUSED(virtualKey)
    Q_UNUSED(down)
    return false;
#endif
}

bool InputHandler::dispatchNativeKeyEvent(int virtualKey, bool down)
{
#ifdef Q_OS_WIN
    bool consumed = false;
    for (const QPointer<InputHandler>& handler : std::as_const(g_keyboardHandlers)) {
        if (handler && handler->handleNativeKeyEvent(virtualKey, down)) {
            consumed = true;
            // Focus can move while a key is held or repeating. Release every
            // guest that owns it, even when the first handler consumes key-up.
            if (down) break;
        }
    }
    return consumed;
#else
    Q_UNUSED(virtualKey)
    Q_UNUSED(down)
    return false;
#endif
}

bool InputHandler::dispatchNativeMouseEvent(const QPoint screenPosition,
                                            const Qt::MouseButton button,
                                            const bool down,
                                            const bool move)
{
#ifdef Q_OS_WIN
    // Mouse input is intentionally not intercepted globally. A real embedded
    // SDL guest receives native pointer messages directly, which is more
    // reliable than swallowing Windows input and replaying it via QMP.
    // This entry point remains available for controlled automation/tests.
    for (const QPointer<InputHandler>& handler : std::as_const(g_keyboardHandlers)) {
        if (handler && handler->handleNativeMouseEvent(screenPosition, button, down, move)) {
            return true;
        }
    }
#else
    Q_UNUSED(screenPosition)
    Q_UNUSED(button)
    Q_UNUSED(down)
    Q_UNUSED(move)
#endif
    return false;
}

bool InputHandler::isEmbeddedDisplayFocused() const
{
#ifdef Q_OS_WIN
    auto* display = qobject_cast<DisplayEmbed*>(m_displayWidget.data());
    if (!display || !display->hasEmbeddedWindow()) {
        return false;
    }
    const HWND qemuWindow = reinterpret_cast<HWND>(display->embeddedWindow());
    if (!IsWindow(qemuWindow)) {
        return false;
    }

    const HWND foregroundWindow = GetForegroundWindow();
    if (GetAncestor(foregroundWindow, GA_ROOT)
        != GetAncestor(qemuWindow, GA_ROOT)) {
        return false;
    }

    GUITHREADINFO threadInfo{};
    threadInfo.cbSize = sizeof(threadInfo);
    const DWORD threadId = GetWindowThreadProcessId(qemuWindow, nullptr);
    if (!threadId || !GetGUIThreadInfo(threadId, &threadInfo)) {
        return false;
    }
    return threadInfo.hwndFocus == qemuWindow
        || IsChild(qemuWindow, threadInfo.hwndFocus);
#else
    return false;
#endif
}

bool InputHandler::isPointerOverEmbeddedDisplay(const QPoint screenPosition) const
{
#ifdef Q_OS_WIN
    auto* display = qobject_cast<DisplayEmbed*>(m_displayWidget.data());
    if (!display || !display->hasEmbeddedWindow()) {
        return false;
    }
    // SDL/OpenGL can expose an internal child below the reparented QEMU HWND,
    // making WindowFromPoint() unreliable. The native DisplayEmbed HWND is
    // stable and covers only the Android viewport, never Sabi's controls.
    const HWND displayWindow = reinterpret_cast<HWND>(display->winId());
    if (!IsWindow(displayWindow)) {
        return false;
    }
    RECT displayRect{};
    if (!GetWindowRect(displayWindow, &displayRect)) {
        return false;
    }
    return PtInRect(&displayRect, POINT{screenPosition.x(), screenPosition.y()});
#else
    Q_UNUSED(screenPosition)
    return false;
#endif
}

bool InputHandler::handleNativeMouseEvent(const QPoint screenPosition,
                                          const Qt::MouseButton button,
                                          const bool down,
                                          const bool move)
{
#ifdef Q_OS_WIN
    auto* display = qobject_cast<DisplayEmbed*>(m_displayWidget.data());
    if (!display || !display->hasEmbeddedWindow()) {
        return false;
    }
    const HWND qemuWindow = reinterpret_cast<HWND>(display->embeddedWindow());
    if (!IsWindow(qemuWindow)) {
        return false;
    }

    const bool ownsButton = button != Qt::NoButton
        && m_pressedButtons.contains(static_cast<int>(button));
    // A focused embedded SDL child must not capture pointer messages meant for
    // Sabi's toolbar, title bar, menus, or the Windows caption controls.
    // Only forward while the pointer is inside the actual child, plus the
    // matching release after a drag began inside it.
    if (!isPointerOverEmbeddedDisplay(screenPosition) && !ownsButton) {
        return false;
    }

    if (down) {
        display->focusEmbeddedWindow();
    }

    POINT position{screenPosition.x(), screenPosition.y()};
    if (!ScreenToClient(qemuWindow, &position)) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(qemuWindow, &client)) {
        return false;
    }
    const int clientWidth = qMax(1, client.right - client.left);
    const int clientHeight = qMax(1, client.bottom - client.top);
    QJsonArray events{
        absoluteAxisEvent(QStringLiteral("x"), scaleToQmpAxis(position.x, clientWidth)),
        absoluteAxisEvent(QStringLiteral("y"), scaleToQmpAxis(position.y, clientHeight))
    };
    if (!move && button != Qt::NoButton) {
        const QString qmpButton = qmpButtonName(button);
        if (qmpButton.isEmpty()) {
            return false;
        }
        events.append(buttonEvent(qmpButton, down));
    }

    const bool sent = sendInputEvents(events);
    if (sent && !move && button != Qt::NoButton) {
        if (down) {
            m_pressedButtons.insert(static_cast<int>(button));
        } else {
            m_pressedButtons.remove(static_cast<int>(button));
        }
    }
    return sent;
#else
    Q_UNUSED(screenPosition)
    Q_UNUSED(button)
    Q_UNUSED(down)
    Q_UNUSED(move)
    return false;
#endif
}

void InputHandler::sendTouchPosition(const int x, const int y, const bool down)
{
    const QSize resolution = m_keymapEngine.guestResolution();
    const int xExtent = resolution.width() > 0 ? resolution.width()
                                               : (m_displayWidget ? m_displayWidget->width() : 1);
    const int yExtent = resolution.height() > 0 ? resolution.height()
                                                 : (m_displayWidget ? m_displayWidget->height() : 1);
    QJsonArray events{
        absoluteAxisEvent(QStringLiteral("x"), scaleToQmpAxis(x, xExtent)),
        absoluteAxisEvent(QStringLiteral("y"), scaleToQmpAxis(y, yExtent)),
        buttonEvent(QStringLiteral("left"), down)
    };
    sendInputEvents(events);
}

void InputHandler::sendTouchMove(const int x, const int y)
{
    const QSize resolution = m_keymapEngine.guestResolution();
    const int xExtent = resolution.width() > 0 ? resolution.width()
                                               : (m_displayWidget ? m_displayWidget->width() : 1);
    const int yExtent = resolution.height() > 0 ? resolution.height()
                                                 : (m_displayWidget ? m_displayWidget->height() : 1);
    sendInputEvents({absoluteAxisEvent(QStringLiteral("x"), scaleToQmpAxis(x, xExtent)),
                     absoluteAxisEvent(QStringLiteral("y"), scaleToQmpAxis(y, yExtent))});
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
    return m_controller && !events.isEmpty()
        && !m_controller->sendInputEvents(events).isEmpty();
}
