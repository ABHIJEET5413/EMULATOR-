#pragma once

#include <QIcon>

/**
 * @file icon_factory.h
 * @brief Supplies named original Sabi SVG icons from the application resources.
 */
enum class SabiIcon {
    Mark, Home, Back, Recents, VolumeUp, VolumeDown, Power, Rotate, Screenshot,
    InstallApk, Fullscreen, Settings, Instances, Phone, Tablet, Desktop,
    StatusReady, StatusWarning, InputDiagnostics, ControlsStudio
};

/** Provides a stable named access point for Sabi's bundled vector icons. */
class IconFactory final {
public:
    /** Return the requested high-DPI vector icon, or an empty icon if unavailable. */
    static QIcon icon(SabiIcon icon);
};
