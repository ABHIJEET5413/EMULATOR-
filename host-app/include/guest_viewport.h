#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

/**
 * @file guest_viewport.h
 * @brief Defines the single DPI-aware geometry contract for Sabi's Android viewport.
 */
class GuestViewport final {
public:
    /** Build a letterboxed viewport from logical host pixels, guest size, and device scale. */
    GuestViewport(QSize hostLogicalSize = {}, QSize guestResolution = {1080, 1920}, qreal devicePixelRatio = 1.0);

    /** Return the centered aspect-correct Android rectangle in logical Qt pixels. */
    QRect logicalContentRect() const;
    /** Return the same Android rectangle in physical Win32 pixels. */
    QRect physicalContentRect() const;
    /** Return whether a logical widget point is inside Android rather than a letterbox margin. */
    bool containsLogical(QPoint point) const;
    /** Map a logical viewport point to QMP's 0..32767 absolute coordinate range. */
    QPoint mapLogicalToQmp(QPoint point) const;

    /** Map a physical native child-client point to QMP's absolute coordinate range. */
    static QPoint mapClientToQmp(QPoint clientPoint, QSize clientSize);
    /** Calculate an aspect-correct centered rectangle without requiring a widget. */
    static QRect calculateLogicalContentRect(QSize hostLogicalSize, QSize guestResolution);

private:
    QSize m_hostLogicalSize;
    QSize m_guestResolution;
    qreal m_devicePixelRatio{1.0};
};
