#include <QLoggingCategory>
#include <QResizeEvent>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "display_embed.h"

namespace {

#ifdef Q_OS_WIN
struct WindowSearch final {
    DWORD processId{0};
    HWND result{nullptr};
};

BOOL CALLBACK findProcessWindow(HWND window, LPARAM data)
{
    auto* search = reinterpret_cast<WindowSearch*>(data);
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(window, &windowProcessId);
    if (windowProcessId != search->processId
        || !IsWindowVisible(window)
        || GetParent(window) != nullptr
        || (GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) != 0) {
        return TRUE;
    }

    search->result = window;
    return FALSE;
}
#endif

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

} // namespace

DisplayEmbed::DisplayEmbed(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_NativeWindow);
    setMinimumSize(240, 320);
}

quintptr DisplayEmbed::findWindowForProcess(const qint64 processId)
{
#ifdef Q_OS_WIN
    if (processId <= 0) {
        return 0;
    }

    WindowSearch search;
    search.processId = static_cast<DWORD>(processId);
    EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&search));
    return reinterpret_cast<quintptr>(search.result);
#else
    Q_UNUSED(processId)
    return 0;
#endif
}

bool DisplayEmbed::embedWindow(const quintptr windowHandle, QString* errorMessage)
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window || !IsWindow(window)) {
        setError(errorMessage, QStringLiteral("The QEMU display window is no longer available."));
        return false;
    }
    if (m_embeddedWindow == windowHandle) {
        resizeEmbeddedWindow();
        return true;
    }
    if (m_embeddedWindow != 0) {
        clearEmbeddedWindow();
    }

    const HWND parentWindow = reinterpret_cast<HWND>(winId());
    const LONG_PTR originalStyle = GetWindowLongPtrW(window, GWL_STYLE);
    const LONG_PTR originalExtendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const HWND originalParent = GetParent(window);

    LONG_PTR childStyle = originalStyle;
    childStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX
                    | WS_SYSMENU | WS_POPUP);
    childStyle |= WS_CHILD;

    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previousStyle = SetWindowLongPtrW(window, GWL_STYLE, childStyle);
    if (previousStyle == 0 && GetLastError() != ERROR_SUCCESS) {
        setError(errorMessage, QStringLiteral("Windows could not update the QEMU window style."));
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    const HWND previousParent = SetParent(window, parentWindow);
    if (!previousParent && GetLastError() != ERROR_SUCCESS) {
        SetWindowLongPtrW(window, GWL_STYLE, originalStyle);
        setError(errorMessage,
                 QStringLiteral("Windows could not embed the QEMU display. Try running both apps at the same privilege level."));
        return false;
    }

    if (!SetWindowPos(window,
                      nullptr,
                      0,
                      0,
                      qMax(1, qRound(width() * devicePixelRatioF())),
                      qMax(1, qRound(height() * devicePixelRatioF())),
                      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW)) {
        SetParent(window, originalParent);
        SetWindowLongPtrW(window, GWL_STYLE, originalStyle);
        SetWindowLongPtrW(window, GWL_EXSTYLE, originalExtendedStyle);
        setError(errorMessage, QStringLiteral("Windows could not size the embedded QEMU display."));
        return false;
    }

    m_embeddedWindow = windowHandle;
    m_originalParent = reinterpret_cast<quintptr>(originalParent);
    m_originalStyle = static_cast<qintptr>(originalStyle);
    m_originalExtendedStyle = static_cast<qintptr>(originalExtendedStyle);
    qInfo() << "Embedded QEMU window" << windowHandle << "in the DroidBox display area.";
    return true;
#else
    Q_UNUSED(windowHandle)
    setError(errorMessage, QStringLiteral("QEMU display embedding is supported on Windows only."));
    return false;
#endif
}

bool DisplayEmbed::hasEmbeddedWindow() const
{
    return m_embeddedWindow != 0;
}

quintptr DisplayEmbed::embeddedWindow() const
{
    return m_embeddedWindow;
}

void DisplayEmbed::focusEmbeddedWindow()
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
    if (window && IsWindow(window)) {
        const DWORD currentThread = GetCurrentThreadId();
        const DWORD windowThread = GetWindowThreadProcessId(window, nullptr);
        const bool attachedInput = windowThread != 0 && windowThread != currentThread
            && AttachThreadInput(currentThread, windowThread, TRUE);
        const HWND rootWindow = GetAncestor(window, GA_ROOT);
        if (rootWindow) {
            SetForegroundWindow(rootWindow);
        }
        SetFocus(window);
        if (attachedInput) {
            AttachThreadInput(currentThread, windowThread, FALSE);
        }
    }
#endif
}

void DisplayEmbed::clearEmbeddedWindow()
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
    if (window && IsWindow(window)) {
        SetParent(window, reinterpret_cast<HWND>(m_originalParent));
        SetWindowLongPtrW(window, GWL_STYLE, static_cast<LONG_PTR>(m_originalStyle));
        SetWindowLongPtrW(window, GWL_EXSTYLE,
                          static_cast<LONG_PTR>(m_originalExtendedStyle));
        SetWindowPos(window,
                     nullptr,
                     0,
                     0,
                     0,
                     0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
#endif
    m_embeddedWindow = 0;
    m_originalParent = 0;
    m_originalStyle = 0;
    m_originalExtendedStyle = 0;
}

void DisplayEmbed::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    resizeEmbeddedWindow();
}

void DisplayEmbed::resizeEmbeddedWindow()
{
#ifdef Q_OS_WIN
    const HWND window = reinterpret_cast<HWND>(m_embeddedWindow);
    if (window && IsWindow(window)) {
        const qreal scale = devicePixelRatioF();
        const int pixelWidth = qMax(1, qRound(width() * scale));
        const int pixelHeight = qMax(1, qRound(height() * scale));
        if (!MoveWindow(window, 0, 0, pixelWidth, pixelHeight, TRUE)) {
            qWarning() << "Could not resize the embedded QEMU display.";
        }
    }
#endif
}
