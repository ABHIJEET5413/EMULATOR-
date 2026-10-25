#pragma once

#include <QObject>
#include <QJsonArray>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QSet>
#include <QString>

#include "keymap_engine.h"

class QEvent;
class QKeyEvent;
class QMouseEvent;
class QWidget;
class QemuController;

/**
 * @file input_handler.h
 * @brief Forwards Qt display-area input to the guest through QMP when needed.
 */
class InputHandler final : public QObject {
    Q_OBJECT

public:
    /** Create an input filter. */
    explicit InputHandler(QObject* parent = nullptr);
    ~InputHandler() override;

    /** Install this handler on the display widget and connect it to QEMU. */
    void installOn(QWidget* displayWidget, QemuController* controller);

    /** Load a JSON profile into the keymap engine. */
    bool loadKeymapProfile(const QString& path, QString* errorMessage = nullptr);

    /** Enable or disable key mapping. */
    void setKeymapEnabled(bool enabled);

    /** Return whether key mapping is enabled. */
    bool isKeymapEnabled() const;

    /** Set the guest resolution used to scale keymap coordinates. */
    void setGuestResolution(QSize resolution);

    /** Dispatch a Windows low-level keyboard event when the QEMU child has focus. */
    bool handleNativeKeyEvent(int virtualKey, bool down);
    /** Route a native keyboard event to the focused guest and release all owners on key-up. */
    static bool dispatchNativeKeyEvent(int virtualKey, bool down);

    /** Route a native mouse event over the embedded QEMU child window through QMP. */
    static bool dispatchNativeMouseEvent(QPoint screenPosition, Qt::MouseButton button,
                                         bool down, bool move);

    /** Convert a widget coordinate to QMP's 0..32767 absolute range. */
    static int scaleToQmpAxis(int coordinate, int extent);

    /** Map a Qt key to QMP's XT set-1 numeric key value. */
    static int keyCodeForQtKey(int qtKey);

protected:
    /** Observe display focus and forward mouse/keyboard events when Qt receives them. */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleMouseEvent(QMouseEvent* event);
    bool handleKeyEvent(QKeyEvent* event, bool down);
    bool sendKeyToGuest(Qt::Key key, bool down);
    bool handleNativeMouseEvent(QPoint screenPosition, Qt::MouseButton button, bool down, bool move);
    void sendTouchPosition(int x, int y, bool down);
    void sendTouchMove(int x, int y);
    void releaseHeldInput();
    bool isEmbeddedDisplayFocused() const;
    bool isPointerOverEmbeddedDisplay(QPoint screenPosition) const;
    bool sendInputEvents(const QJsonArray& events);

    QPointer<QWidget> m_displayWidget;
    QPointer<QemuController> m_controller;
    KeymapEngine m_keymapEngine;
    QSet<int> m_pressedButtons;
    QSet<int> m_pressedKeys;
};
