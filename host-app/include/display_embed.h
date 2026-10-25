#pragma once

#include <QRect>
#include <QString>
#include <QSize>
#include <QWidget>

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

    /** Set the native guest raster size used to preserve aspect ratio on resize. */
    void setGuestResolution(QSize resolution);

    /** Calculate the centered guest rectangle without stretching its aspect ratio. */
    static QRect guestContentRect(QSize availableSize, QSize guestResolution);

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
    QSize m_guestResolution{1080, 1920};
};
