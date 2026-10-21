#pragma once

#include <QWidget>

#include <QString>

/**
 * @file display_embed.h
 * @brief Reparents QEMU's native display window into a Qt widget.
 */
class DisplayEmbed final : public QWidget {
    Q_OBJECT

public:
    /** Create a focusable display area for the QEMU window. */
    explicit DisplayEmbed(QWidget* parent = nullptr);

    /** Find a visible top-level window owned by the given process ID. */
    static quintptr findWindowForProcess(qint64 processId);

    /** Reparent a native QEMU window into this widget. */
    bool embedWindow(quintptr windowHandle, QString* errorMessage = nullptr);

    /** Return whether a QEMU window is currently embedded. */
    bool hasEmbeddedWindow() const;

    /** Return the embedded native window handle. */
    quintptr embeddedWindow() const;

    /** Ask Windows to focus the embedded QEMU window. */
    void focusEmbeddedWindow();

    /** Restore the original window styles and parent if the window still exists. */
    void clearEmbeddedWindow();

protected:
    /** Resize the embedded QEMU window to the widget's client area. */
    void resizeEvent(QResizeEvent* event) override;

private:
    /** Match the native QEMU client size to this widget. */
    void resizeEmbeddedWindow();

    quintptr m_embeddedWindow{0};
    quintptr m_originalParent{0};
    qintptr m_originalStyle{0};
    qintptr m_originalExtendedStyle{0};
};
