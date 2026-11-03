#pragma once

#include <QColor>
#include <QString>

class QApplication;
class QWidget;

/**
 * @file ui_theme.h
 * @brief Provides Sabi's shared palette, production widget style, and window polish.
 */
namespace UiTheme {

/** Install Sabi's high-DPI-aware application palette and widget style. */
void install(QApplication& application);

/** Apply Sabi's window identity to a top-level widget. */
void polishWindow(QWidget* window, const QString& objectName);

/** Return the current Sabi accent color for status and custom painting. */
QColor accentColor();

/** Return the color representing a ready/connected service state. */
QColor readyColor();

/** Return the color representing a warning or unavailable state. */
QColor warningColor();

} // namespace UiTheme
