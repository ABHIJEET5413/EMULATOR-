#include <QHash>

#include "icon_factory.h"

QIcon IconFactory::icon(const SabiIcon icon)
{
    static const QHash<SabiIcon, QString> paths{
        {SabiIcon::Mark, QStringLiteral(":/sabi/icons/sabi-mark.svg")},
        {SabiIcon::Home, QStringLiteral(":/sabi/icons/home.svg")},
        {SabiIcon::Back, QStringLiteral(":/sabi/icons/back.svg")},
        {SabiIcon::Recents, QStringLiteral(":/sabi/icons/recents.svg")},
        {SabiIcon::VolumeUp, QStringLiteral(":/sabi/icons/volume-up.svg")},
        {SabiIcon::VolumeDown, QStringLiteral(":/sabi/icons/volume-down.svg")},
        {SabiIcon::Power, QStringLiteral(":/sabi/icons/power.svg")},
        {SabiIcon::Rotate, QStringLiteral(":/sabi/icons/rotate.svg")},
        {SabiIcon::Screenshot, QStringLiteral(":/sabi/icons/screenshot.svg")},
        {SabiIcon::InstallApk, QStringLiteral(":/sabi/icons/install-apk.svg")},
        {SabiIcon::Fullscreen, QStringLiteral(":/sabi/icons/fullscreen.svg")},
        {SabiIcon::Settings, QStringLiteral(":/sabi/icons/settings.svg")},
        {SabiIcon::Instances, QStringLiteral(":/sabi/icons/instances.svg")},
        {SabiIcon::Phone, QStringLiteral(":/sabi/icons/phone.svg")},
        {SabiIcon::Tablet, QStringLiteral(":/sabi/icons/tablet.svg")},
        {SabiIcon::Desktop, QStringLiteral(":/sabi/icons/desktop.svg")},
        {SabiIcon::StatusReady, QStringLiteral(":/sabi/icons/status-ready.svg")},
        {SabiIcon::StatusWarning, QStringLiteral(":/sabi/icons/status-warning.svg")},
        {SabiIcon::InputDiagnostics, QStringLiteral(":/sabi/icons/input-diagnostics.svg")},
        {SabiIcon::ControlsStudio, QStringLiteral(":/sabi/icons/controls-studio.svg")}
    };
    return QIcon(paths.value(icon));
}
