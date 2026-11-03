#include <QCoreApplication>
#include <QTextStream>

#include "guest_viewport.h"

namespace {

bool check(const QString& name, const bool passed)
{
    QTextStream(passed ? stdout : stderr) << (passed ? "[PASS] " : "[FAIL] ") << name << Qt::endl;
    return passed;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool allPassed = true;

    const GuestViewport landscape({1600, 1000}, {1920, 1080}, 1.0);
    allPassed &= check(QStringLiteral("landscape guest is centered without stretching"),
                       landscape.logicalContentRect() == QRect(0, 50, 1600, 900));
    allPassed &= check(QStringLiteral("letterbox margins reject guest input"),
                       !landscape.containsLogical({400, 20}) && landscape.containsLogical({400, 50}));
    allPassed &= check(QStringLiteral("logical viewport edges map to full QMP range"),
                       landscape.mapLogicalToQmp({0, 50}) == QPoint(0, 0)
                           && landscape.mapLogicalToQmp({1599, 949}) == QPoint(32767, 32767));

    const GuestViewport portrait({1000, 1600}, {1080, 1920}, 1.25);
    allPassed &= check(QStringLiteral("portrait guest is centered without stretching"),
                       portrait.logicalContentRect() == QRect(50, 0, 900, 1600));
    allPassed &= check(QStringLiteral("physical viewport uses per-monitor DPI scale"),
                       portrait.physicalContentRect() == QRect(63, 0, 1125, 2000));
    allPassed &= check(QStringLiteral("portrait side margins reject guest input"),
                       !portrait.containsLogical({49, 300}) && portrait.containsLogical({50, 300}));

    const QPoint clamped = GuestViewport::mapClientToQmp({9999, -5}, {100, 50});
    allPassed &= check(QStringLiteral("native child client mapping clamps safely"), clamped == QPoint(32767, 0));
    allPassed &= check(QStringLiteral("single-pixel native client mapping is safe"),
                       GuestViewport::mapClientToQmp({0, 0}, {1, 1}) == QPoint(0, 0));
    allPassed &= check(QStringLiteral("fractional DPI preserves full native edge range"),
                       GuestViewport::mapClientToQmp({1124, 1999}, {1125, 2000}) == QPoint(32767, 32767));

    return allPassed ? 0 : 1;
}
