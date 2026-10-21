#include <QCoreApplication>
#include <QTextStream>

#include "input_handler.h"

namespace {

bool check(const QString& name, const bool passed)
{
    QTextStream stream(passed ? stdout : stderr);
    stream << (passed ? "[PASS] " : "[FAIL] ") << name << Qt::endl;
    return passed;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_display_input"));

    bool allPassed = true;
    allPassed &= check(QStringLiteral("left edge maps to QMP coordinate 0"),
                       InputHandler::scaleToQmpAxis(0, 100) == 0);
    allPassed &= check(QStringLiteral("right edge maps to QMP coordinate 32767"),
                       InputHandler::scaleToQmpAxis(99, 100) == 32767);
    allPassed &= check(QStringLiteral("out-of-range coordinates are clamped"),
                       InputHandler::scaleToQmpAxis(120, 100) == 32767
                           && InputHandler::scaleToQmpAxis(-1, 100) == 0);
    allPassed &= check(QStringLiteral("single-pixel extent maps safely"),
                       InputHandler::scaleToQmpAxis(0, 1) == 0);
    allPassed &= check(QStringLiteral("Qt letter keys map to QMP keycodes"),
                       InputHandler::keyCodeForQtKey(Qt::Key_A) == 30
                           && InputHandler::keyCodeForQtKey(Qt::Key_Z) == 44);
    allPassed &= check(QStringLiteral("Enter uses QMP keycode 28"),
                       InputHandler::keyCodeForQtKey(Qt::Key_Return) == 28);
    allPassed &= check(QStringLiteral("function and navigation keys map"),
                       InputHandler::keyCodeForQtKey(Qt::Key_F12) == 88
                           && InputHandler::keyCodeForQtKey(Qt::Key_Left) == 0xcb
                           && InputHandler::keyCodeForQtKey(Qt::Key_Delete) == 0xd3
                           && InputHandler::keyCodeForQtKey(Qt::Key_Home) == 0xc7);
    allPassed &= check(QStringLiteral("unsupported keys are left to Qt/QEMU"),
                       InputHandler::keyCodeForQtKey(Qt::Key_unknown) == -1);

    return allPassed ? 0 : 1;
}
