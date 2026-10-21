#pragma once

#include <QObject>
#include <QJsonArray>
#include <QPointer>
#include <QSet>

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

    /** Install this handler on the display widget and connect it to QEMU. */
    void installOn(QWidget* displayWidget, QemuController* controller);

    /** Convert a widget coordinate to QMP's 0..32767 absolute range. */
    static int scaleToQmpAxis(int coordinate, int extent);

    /** Map a Qt key to the Linux input keycode used by QMP numeric key values. */
    static int keyCodeForQtKey(int qtKey);

protected:
    /** Observe display focus and forward mouse/keyboard events when Qt receives them. */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool handleMouseEvent(QMouseEvent* event);
    bool handleKeyEvent(QKeyEvent* event, bool down);
    void releaseHeldInput();
    bool sendInputEvents(const QJsonArray& events);

    QPointer<QWidget> m_displayWidget;
    QPointer<QemuController> m_controller;
    QSet<int> m_pressedButtons;
    QSet<int> m_pressedKeys;
};
