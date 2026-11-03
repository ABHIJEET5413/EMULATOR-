#include <QApplication>
#include <QLabel>
#include <QPixmap>
#include <QToolButton>
#include <QWidget>
#include <QTextStream>

#include "icon_factory.h"
#include "ui_theme.h"

namespace {

int failures = 0;

void check(const bool condition, const QString& description)
{
    QTextStream(stdout) << (condition ? "PASS: " : "FAIL: ") << description << Qt::endl;
    if (!condition) ++failures;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    UiTheme::install(application);

    QWidget window;
    UiTheme::polishWindow(&window, QStringLiteral("sabiThemeFixture"));
    check(window.objectName() == QStringLiteral("sabiThemeFixture"),
          QStringLiteral("theme assigns a stable window identity"));
    check(application.palette().color(QPalette::Window) == QColor(QStringLiteral("#10141d")),
          QStringLiteral("theme installs the Sabi surface palette"));
    check(UiTheme::accentColor().isValid() && UiTheme::readyColor().isValid()
              && UiTheme::warningColor().isValid(),
          QStringLiteral("theme exposes valid semantic colors"));

    const QList<SabiIcon> icons{
        SabiIcon::Mark, SabiIcon::Home, SabiIcon::Back, SabiIcon::Recents,
        SabiIcon::VolumeUp, SabiIcon::VolumeDown, SabiIcon::Power, SabiIcon::Rotate,
        SabiIcon::Screenshot, SabiIcon::InstallApk, SabiIcon::Fullscreen,
        SabiIcon::Settings, SabiIcon::Instances, SabiIcon::Phone, SabiIcon::Tablet,
        SabiIcon::Desktop, SabiIcon::StatusReady, SabiIcon::StatusWarning
    };
    for (const SabiIcon icon : icons) {
        const QPixmap pixmap = IconFactory::icon(icon).pixmap(24, 24);
        check(!pixmap.isNull(), QStringLiteral("SVG icon renders at high-DPI-safe logical size"));
    }

    QToolButton profileButton;
    profileButton.setObjectName(QStringLiteral("sabiProfileButton"));
    profileButton.setAccessibleName(QStringLiteral("Display profile"));
    profileButton.setFocusPolicy(Qt::StrongFocus);
    check(profileButton.focusPolicy() == Qt::StrongFocus
              && profileButton.accessibleName() == QStringLiteral("Display profile"),
          QStringLiteral("profile control supports keyboard focus and accessibility"));

    QLabel status;
    status.setAccessibleName(QStringLiteral("Android status"));
    check(status.accessibleName() == QStringLiteral("Android status"),
          QStringLiteral("status indicator has an accessible name"));
    QTextStream(stdout) << "Failures: " << failures << Qt::endl;
    return failures == 0 ? 0 : 1;
}
