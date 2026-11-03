#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#include "ui_theme.h"

namespace {

constexpr auto SURFACE = "#10141d";
constexpr auto SURFACE_RAISED = "#171d29";
constexpr auto SURFACE_HOVER = "#222b3b";
constexpr auto BORDER = "#2d3748";
constexpr auto TEXT = "#edf2f7";
constexpr auto TEXT_MUTED = "#9aa7ba";
constexpr auto ACCENT = "#6ea8fe";
constexpr auto ACCENT_HOVER = "#8bb8ff";
constexpr auto READY = "#58d68d";
constexpr auto WARNING = "#f6bd60";

class SabiProxyStyle final : public QProxyStyle {
public:
    explicit SabiProxyStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

    int pixelMetric(const PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override
    {
        switch (metric) {
        case PM_ToolBarItemSpacing: return 6;
        case PM_SmallIconSize: return 20;
        case PM_DefaultFrameWidth: return 1;
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

QPalette sabiPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(SURFACE));
    palette.setColor(QPalette::WindowText, QColor(TEXT));
    palette.setColor(QPalette::Base, QColor(SURFACE_RAISED));
    palette.setColor(QPalette::AlternateBase, QColor(SURFACE));
    palette.setColor(QPalette::Text, QColor(TEXT));
    palette.setColor(QPalette::Button, QColor(SURFACE_RAISED));
    palette.setColor(QPalette::ButtonText, QColor(TEXT));
    palette.setColor(QPalette::Highlight, QColor(ACCENT));
    palette.setColor(QPalette::HighlightedText, QColor("#08111f"));
    palette.setColor(QPalette::ToolTipBase, QColor("#e8eef8"));
    palette.setColor(QPalette::ToolTipText, QColor("#10141d"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(TEXT_MUTED));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(TEXT_MUTED));
    return palette;
}

QString styleSheet()
{
    return QStringLiteral(R"(
QMainWindow#sabiMainWindow, QWidget#sabiInstanceWindow, QDialog#sabiSettingsDialog { background: %1; color: %2; }
QWidget#sabiShell { background: %1; }
QFrame#sabiContextBar { background: %3; border-bottom: 1px solid %4; }
QLabel#sabiWordmark { color: %2; font-size: 15px; font-weight: 700; letter-spacing: 2px; }
QLabel#sabiInstanceName { color: %6; font-weight: 600; }
QLabel#sabiContextStatus { color: %6; }
QToolBar#sabiToolRail { background: %3; border: 0; border-left: 1px solid %4; spacing: 6px; padding: 10px 8px; }
QToolBar#sabiToolRail::separator { height: 1px; background: %4; margin: 7px 5px; }
QToolBar#sabiToolRail QToolButton, QToolButton#sabiProfileButton { background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 8px; min-width: 36px; min-height: 36px; }
QToolBar#sabiToolRail QToolButton:hover, QToolButton#sabiProfileButton:hover { background: %5; border-color: %4; }
QToolBar#sabiToolRail QToolButton:checked, QToolButton#sabiProfileButton:checked { background: #1d4f91; border-color: %7; }
QToolBar#sabiToolRail QToolButton:focus, QToolButton#sabiProfileButton:focus, QPushButton:focus, QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QTableWidget:focus { outline: none; border: 1px solid %7; }
QToolBar#sabiToolRail QToolButton:disabled { color: %6; }
QStatusBar#sabiStatusBar { background: %3; color: %6; border-top: 1px solid %4; padding: 2px 8px; }
QPushButton { background: #253149; border: 1px solid %4; border-radius: 7px; color: %2; min-height: 30px; padding: 4px 12px; }
QPushButton:hover { background: %5; border-color: %7; }
QPushButton:pressed { background: #1d4f91; }
QComboBox, QSpinBox, QLineEdit { background: %3; border: 1px solid %4; border-radius: 6px; color: %2; min-height: 26px; padding: 2px 7px; }
QComboBox QAbstractItemView { background: %3; border: 1px solid %4; color: %2; selection-background-color: #1d4f91; }
QTableWidget { background: %3; alternate-background-color: %1; border: 1px solid %4; gridline-color: %4; color: %2; selection-background-color: #1d4f91; }
QHeaderView::section { background: #202838; border: 0; border-bottom: 1px solid %4; color: %6; font-weight: 600; padding: 7px; }
QMenu { background: %3; border: 1px solid %4; color: %2; padding: 5px; }
QMenu::item { border-radius: 5px; padding: 7px 26px 7px 9px; }
QMenu::item:selected { background: %5; }
QToolTip { background: #e8eef8; color: #10141d; border: 0; padding: 5px; }
QListWidget#sabiSettingsNav { background: %3; border: 1px solid %4; border-radius: 8px; padding: 5px; outline: none; }
QListWidget#sabiSettingsNav::item { border-radius: 6px; color: %6; min-height: 28px; padding: 6px 10px; }
QListWidget#sabiSettingsNav::item:hover { background: %5; color: %2; }
QListWidget#sabiSettingsNav::item:selected { background: #1d4f91; color: %2; }
QLabel#sabiSettingsTitle { color: %2; font-size: 18px; font-weight: 700; }
QLabel#sabiSettingsNotice { background: #18253a; border: 1px solid #294a75; border-radius: 7px; color: %2; padding: 8px; }
QGroupBox#sabiSettingsGroup { border: 1px solid %4; border-radius: 8px; margin-top: 10px; padding: 12px 10px 8px 10px; color: %2; font-weight: 600; }
QGroupBox#sabiSettingsGroup::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QToolButton[profile] { background: %3; border: 1px solid %4; border-radius: 8px; color: %2; min-height: 38px; padding: 7px 10px; }
QToolButton[profile]:hover { background: %5; border-color: %7; }
QToolButton[profile]:checked { background: #1d4f91; border-color: %7; }
)").arg(QString::fromLatin1(SURFACE), QString::fromLatin1(TEXT), QString::fromLatin1(SURFACE_RAISED),
         QString::fromLatin1(BORDER), QString::fromLatin1(SURFACE_HOVER), QString::fromLatin1(TEXT_MUTED),
         QString::fromLatin1(ACCENT_HOVER));
}

} // namespace

void initializeSabiResources()
{
    Q_INIT_RESOURCE(sabi_ui);
}

void UiTheme::install(QApplication& application)
{
    initializeSabiResources();
    application.setStyle(new SabiProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    application.setPalette(sabiPalette());
    application.setStyleSheet(styleSheet());
}

void UiTheme::polishWindow(QWidget* window, const QString& objectName)
{
    if (window) {
        window->setObjectName(objectName);
        window->setAttribute(Qt::WA_StyledBackground, true);
    }
}

QColor UiTheme::accentColor() { return QColor(ACCENT); }
QColor UiTheme::readyColor() { return QColor(READY); }
QColor UiTheme::warningColor() { return QColor(WARNING); }
