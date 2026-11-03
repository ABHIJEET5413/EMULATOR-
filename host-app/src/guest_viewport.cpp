#include <QtMath>

#include "guest_viewport.h"

GuestViewport::GuestViewport(const QSize hostLogicalSize, const QSize guestResolution,
                             const qreal devicePixelRatio)
    : m_hostLogicalSize(hostLogicalSize)
    , m_guestResolution(guestResolution)
    , m_devicePixelRatio(qMax<qreal>(1.0, devicePixelRatio))
{
}

QRect GuestViewport::calculateLogicalContentRect(const QSize hostLogicalSize,
                                                 const QSize guestResolution)
{
    if (hostLogicalSize.width() < 1 || hostLogicalSize.height() < 1
        || guestResolution.width() < 1 || guestResolution.height() < 1) {
        return {};
    }
    const qreal scale = qMin(static_cast<qreal>(hostLogicalSize.width()) / guestResolution.width(),
                             static_cast<qreal>(hostLogicalSize.height()) / guestResolution.height());
    const QSize content(qMax(1, qRound(guestResolution.width() * scale)),
                        qMax(1, qRound(guestResolution.height() * scale)));
    return QRect((hostLogicalSize.width() - content.width()) / 2,
                 (hostLogicalSize.height() - content.height()) / 2,
                 content.width(), content.height());
}

QRect GuestViewport::logicalContentRect() const
{
    return calculateLogicalContentRect(m_hostLogicalSize, m_guestResolution);
}

QRect GuestViewport::physicalContentRect() const
{
    const QRect logical = logicalContentRect();
    return QRect(qRound(logical.x() * m_devicePixelRatio), qRound(logical.y() * m_devicePixelRatio),
                 qMax(1, qRound(logical.width() * m_devicePixelRatio)),
                 qMax(1, qRound(logical.height() * m_devicePixelRatio)));
}

bool GuestViewport::containsLogical(const QPoint point) const
{
    return logicalContentRect().contains(point);
}

QPoint GuestViewport::mapLogicalToQmp(const QPoint point) const
{
    const QRect content = logicalContentRect();
    return mapClientToQmp(point - content.topLeft(), content.size());
}

QPoint GuestViewport::mapClientToQmp(const QPoint clientPoint, const QSize clientSize)
{
    const auto scaleAxis = [](const int coordinate, const int extent) {
        if (extent <= 1) return 0;
        return qRound(static_cast<qreal>(qBound(0, coordinate, extent - 1)) * 32767.0 / (extent - 1));
    };
    return {scaleAxis(clientPoint.x(), clientSize.width()),
            scaleAxis(clientPoint.y(), clientSize.height())};
}
