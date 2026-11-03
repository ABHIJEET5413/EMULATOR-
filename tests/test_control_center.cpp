#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonObject>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>

#include "diagnostics_service.h"
#include "settings_dialog.h"
#include "ui_theme.h"

namespace {
int failures = 0;
void check(const bool condition, const QString& name)
{
    QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!condition) ++failures;
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    UiTheme::install(app);
    const QJsonObject config{{"name", "office-phone"}, {"cpuCores", 2}, {"ramMB", 3072},
        {"qmpPort", 4444}, {"adbHostPort", 5556}, {"baseImage", "C:/Users/Alice/secret/base.qcow2"},
        {"overlayImage", "C:/Users/Alice/secret/office-phone.qcow2"},
        {"display", QJsonObject{{"profile", "custom"}, {"width", 1200}, {"height", 2000}, {"dpi", 420}}},
        {"camera", QJsonObject{{"mode", "none"}}}};
    SettingsDialog dialog(config, false);
    auto* navigation = dialog.findChild<QListWidget*>(QStringLiteral("sabiSettingsNav"));
    check(navigation && navigation->count() == 8, "control center exposes eight organized pages");
    if (navigation) {
        const QStringList expected{"General", "Display", "Performance", "Controls", "Storage", "Network / ADB", "Advanced", "Diagnostics"};
        QStringList actual;
        for (int row = 0; row < navigation->count(); ++row) actual.append(navigation->item(row)->text());
        check(actual == expected, "control center page labels are stable");
    }
    auto* phone = dialog.findChild<QToolButton*>(QStringLiteral("settingsProfilePhone"));
    check(phone != nullptr, "phone display profile card exists");
    if (phone) {
        phone->click();
        const QJsonObject display = dialog.getConfig().value("display").toObject();
        check(display.value("profile").toString() == "phone" && display.value("width").toInt() == 1080
                  && display.value("height").toInt() == 2400, "phone profile persists canonical dimensions");
    }
    QPushButton* restore = nullptr;
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Restore configurable defaults")) { restore = button; break; }
    }
    check(restore != nullptr, "restore configurable defaults action exists");
    if (restore) restore->click();
    const QJsonObject restored = dialog.getConfig();
    check(restored.value("cpuCores").toInt() == 1 && restored.value("ramMB").toInt() == 2048
              && restored.value("camera").toObject().value("mode").toString() == "none", "restore defaults resets only configurable values");
    check(restored.value("name") == config.value("name") && restored.value("baseImage") == config.value("baseImage")
              && restored.value("overlayImage") == config.value("overlayImage") && restored.value("qmpPort") == config.value("qmpPort"),
          "restore defaults preserves identity, disks, and ports");
    QString validationError;
    const auto ports = dialog.findChildren<QSpinBox*>();
    QSpinBox* qmp = nullptr; QSpinBox* adb = nullptr;
    for (auto* spin : ports) { if (spin->value() == 4444) qmp = spin; if (spin->value() == 5556) adb = spin; }
    if (qmp && adb) qmp->setValue(adb->value());
    check(qmp && adb && !dialog.validate(&validationError) && validationError.contains("different"), "same QMP and ADB ports are rejected before save");
    const QString report = DiagnosticsService::redactedSummary(config);
    check(report.contains("base.qcow2") && report.contains("office-phone.qcow2") && report.contains("<Sabi local data>")
              && !report.contains("Alice") && !report.contains("secret"), "diagnostics redact host paths and retain useful file names");
    QTemporaryDir temporary;
    const QString reportPath = temporary.filePath("support.txt");
    QString error;
    const bool written = DiagnosticsService::saveRedactedReport(reportPath, config, &error);
    QFile file(reportPath);
    check(written && file.open(QIODevice::ReadOnly | QIODevice::Text) && file.readAll().contains("Sabi local support report"), "redacted report saves only by explicit path");
    return failures == 0 ? 0 : 1;
}
