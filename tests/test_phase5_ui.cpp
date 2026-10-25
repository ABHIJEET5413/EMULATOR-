#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "instance_list_window.h"
#include "instance_manager.h"
#include "settings_dialog.h"
#include "display_embed.h"

#include <functional>

namespace {
int failures = 0;
void check(bool success, const QString& name)
{
    QTextStream(stdout) << (success ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!success) ++failures;
}
bool waitUntil(const std::function<bool()>& condition)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < 15000) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    return condition();
}
QWidget* field(SettingsDialog& dialog, const QString& name)
{
    auto* form = dialog.findChild<QFormLayout*>();
    for (int row = 0; row < form->rowCount(); ++row) {
        const auto label = form->itemAt(row, QFormLayout::LabelRole);
        auto* text = label ? qobject_cast<QLabel*>(label->widget()) : nullptr;
        if (text && text->text() == name) return form->itemAt(row, QFormLayout::FieldRole)->widget();
    }
    return nullptr;
}
QPushButton* action(InstanceListWindow& window, const QString& name)
{
    for (auto* button : window.findChildren<QPushButton*>()) if (button->text() == name) return button;
    return nullptr;
}
void select(InstanceListWindow& window, const QString& name)
{
    auto* table = window.findChild<QTableWidget*>();
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->text() == name) { table->setCurrentCell(row, 0); table->selectRow(row); return; }
    }
    check(false, "instance row exists: " + name);
}
void modalAction(InstanceListWindow& window, const QString& name,
                 const std::function<void(QWidget*)>& response)
{
    bool seen = false;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto* modal = QApplication::activeModalWidget(); seen = modal != nullptr;
        if (modal) response(modal);
    });
    timer.start(0);
    auto* button = action(window, name);
    if (button) button->click();
    timer.stop(); check(seen, name + " opens its dialog");
}
QByteArray digest(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256) : QByteArray{};
}
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    const auto root = InstanceManager::findResourceRoot();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const auto base = temporary.filePath("base.qcow2");
    const auto img = QDir(root).filePath("vm/qemu/qemu-img.exe");
    const auto io = QDir(root).filePath("vm/qemu/qemu-io.exe");
    const auto run = [](const QString& tool, const QStringList& args) {
        QProcess process; process.start(tool, args);
        return process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    if (!run(img, {"create", "-f", "qcow2", base, "16M"})) return 1;
    InstanceManager manager(root, temporary.filePath("instances"));
    check(manager.createInstance("ui-original", {{"baseImage", base}, {"cpuCores", 1}, {"ramMB", 1024}}), "create isolated UI fixture");
    const auto config = manager.getConfig("ui-original");
    const auto overlay = config.value("overlayImage").toString();
    if (config.isEmpty()) return 1;
    check(run(io, {"-f", "qcow2", "-c", "write -P 0x5a 0 512", overlay}), "write UI fixture data");
    const auto originalDigest = digest(overlay);

    SettingsDialog settings(config, false);
    auto* cpu = qobject_cast<QSpinBox*>(field(settings, "CPU cores"));
    auto* ram = settings.findChild<QSlider*>();
    auto* resolution = qobject_cast<QComboBox*>(field(settings, "Resolution"));
    auto* profile = qobject_cast<QComboBox*>(field(settings, "Display profile"));
    auto* dpi = qobject_cast<QComboBox*>(field(settings, "DPI"));
    auto* camera = qobject_cast<QComboBox*>(field(settings, "Camera"));
    auto* qmp = qobject_cast<QSpinBox*>(field(settings, "QMP port"));
    auto* adb = qobject_cast<QSpinBox*>(field(settings, "ADB port"));
    check(cpu && ram && profile && resolution && dpi && camera && qmp && adb, "settings expose all required controls");
    if (!cpu || !ram || !profile || !resolution || !dpi || !camera || !qmp || !adb) return 1;
    check(cpu->value() == 1 && ram->value() * 512 == 1024 && qmp->value() == config.value("qmpPort").toInt()
          && adb->value() == config.value("adbHostPort").toInt(), "settings load saved performance and ports");
    check(cpu->minimum() == 1 && cpu->maximum() == InstanceManager::maximumCpuCores(), "CPU widget enforces host limit");
    check(ram->minimum() * 512 == 1024 && ram->maximum() * 512 == InstanceManager::maximumRamMB(), "RAM widget enforces host limit and step");
    profile->setCurrentIndex(profile->findData("custom"));
    for (int index = 0; index < resolution->count(); ++index) {
        resolution->setCurrentIndex(index);
        const auto parts = resolution->currentData().toString().split('x');
        const auto display = settings.getConfig().value("display").toObject();
        check(display.value("width").toInt() == parts[0].toInt() && display.value("height").toInt() == parts[1].toInt(), "resolution preset " + resolution->currentText());
    }
    check(resolution->findText("Mobile — Full HD (1080 × 1920)") >= 0
          && resolution->findText("Tablet — QHD (2560 × 1600)") >= 0
          && resolution->findText("Desktop — Full HD (1920 × 1080)") >= 0,
          "settings identify mobile, tablet, and desktop display presets");
    const QList<QPair<QString, QSize>> profiles{{"phone", {1080, 2400}}, {"tablet", {1600, 2560}}, {"pc", {1920, 1080}}};
    for (const auto& preset : profiles) {
        profile->setCurrentIndex(profile->findData(preset.first));
        const auto savedDisplay = settings.getConfig().value("display").toObject();
        check(savedDisplay.value("profile").toString() == preset.first
                  && QSize(savedDisplay.value("width").toInt(), savedDisplay.value("height").toInt()) == preset.second,
              "display profile persists " + preset.first);
    }
    profile->setCurrentIndex(profile->findData("custom"));
    const QRect desktopRect = DisplayEmbed::guestContentRect(QSize(1600, 1000), QSize(1920, 1080));
    check(desktopRect == QRect(0, 50, 1600, 900), "display embedding centers a 16:9 guest without stretching");
    const QRect mobileRect = DisplayEmbed::guestContentRect(QSize(1000, 1600), QSize(1080, 1920));
    check(mobileRect == QRect(50, 0, 900, 1600), "display embedding centers a portrait guest without stretching");
    for (int index = 0; index < dpi->count(); ++index) {
        dpi->setCurrentIndex(index);
        check(settings.getConfig().value("display").toObject().value("dpi").toInt() == dpi->currentText().toInt(), "DPI selection " + dpi->currentText());
    }
    auto* cameraModel = qobject_cast<QStandardItemModel*>(camera->model());
    check(camera->currentData().toString() == "none" && !cameraModel->item(1)->isEnabled(), "camera defaults to None and disables unsupported test pattern");
    auto* vendor = qobject_cast<QLineEdit*>(field(settings, "USB vendor ID"));
    auto* product = qobject_cast<QLineEdit*>(field(settings, "USB product ID"));
    check(!vendor->isEnabled() && !product->isEnabled(), "None disables USB ID fields");
    camera->setCurrentIndex(2); vendor->setText("0x046d"); product->setText("0x082d");
    const auto selectedCamera = settings.getConfig().value("camera").toObject();
    check(vendor->isEnabled() && product->isEnabled() && selectedCamera.value("vendorId") == "0x046d"
          && selectedCamera.value("productId") == "0x082d", "experimental camera serializes selected USB IDs");
    SettingsDialog running(config, true);
    check(!field(running, "QMP port")->isEnabled() && !field(running, "ADB port")->isEnabled(), "running settings disable port edits");

    InstanceListWindow window(&manager); window.show(); QCoreApplication::processEvents();
    modalAction(window, "Create", [](QWidget* modal) {
        auto* dialog = qobject_cast<QInputDialog*>(modal); dialog->setTextValue("ui-created"); dialog->accept();
    });
    check(waitUntil([&] { return window.isEnabled() && !manager.getConfig("ui-created").isEmpty(); }), "Create button publishes a fresh overlay and config");
    select(window, "ui-original");
    modalAction(window, "Settings", [&](QWidget* modal) {
        auto* dialog = qobject_cast<SettingsDialog*>(modal);
        check(dialog != nullptr, "launcher opens SettingsDialog");
        if (!dialog) return;
        qobject_cast<QSpinBox*>(field(*dialog, "CPU cores"))->setValue(qMin(2, InstanceManager::maximumCpuCores()));
        dialog->findChild<QSlider*>()->setValue(qMin(3, InstanceManager::maximumRamMB() / 512));
        qobject_cast<QComboBox*>(field(*dialog, "Resolution"))->setCurrentIndex(
            qobject_cast<QComboBox*>(field(*dialog, "Resolution"))->findData("1920x1200"));
        qobject_cast<QComboBox*>(field(*dialog, "DPI"))->setCurrentText("320");
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    const auto saved = manager.getConfig("ui-original");
    check(saved.value("cpuCores").toInt() == qMin(2, InstanceManager::maximumCpuCores())
          && saved.value("ramMB").toInt() == qMin(1536, InstanceManager::maximumRamMB())
          && saved.value("display").toObject().value("width").toInt() == 1920
          && saved.value("display").toObject().value("dpi").toInt() == 320, "Save persists CPU, RAM, resolution, and DPI");
    modalAction(window, "Settings", [](QWidget* modal) {
        auto* dialog = qobject_cast<SettingsDialog*>(modal);
        dialog->findChild<QSlider*>()->setValue(2);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();
    });
    check(manager.getConfig("ui-original") == saved, "Cancel preserves saved settings");
    bool conflictReported = false;
    modalAction(window, "Settings", [&](QWidget* modal) {
        auto* dialog = qobject_cast<SettingsDialog*>(modal);
        qobject_cast<QSpinBox*>(field(*dialog, "QMP port"))->setValue(manager.getConfig("ui-created").value("adbHostPort").toInt());
        QTimer::singleShot(0, &window, [&] {
            auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            conflictReported = warning && warning->text().contains("already assigned");
            if (warning) warning->button(QMessageBox::Ok)->click();
            QTimer::singleShot(0, &window, [] {
                if (auto* retry = qobject_cast<SettingsDialog*>(QApplication::activeModalWidget())) retry->reject();
            });
        });
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    });
    check(conflictReported && manager.getConfig("ui-original") == saved, "conflicting port shows a warning and preserves settings");
    modalAction(window, "Clone", [](QWidget* modal) { auto* dialog = qobject_cast<QInputDialog*>(modal); dialog->setTextValue("ui-copy"); dialog->accept(); });
    check(waitUntil([&] { return window.isEnabled() && !manager.getConfig("ui-copy").isEmpty(); }), "Clone button completes asynchronously");
    check(run(io, {"-f", "qcow2", "-c", "read -P 0x5a 0 512", manager.getConfig("ui-copy").value("overlayImage").toString()}), "UI clone preserves guest data");
    select(window, "ui-original");
    for (const QString& name : {QString("Delete"), QString("Factory reset")}) {
        modalAction(window, name, [](QWidget* modal) { qobject_cast<QMessageBox*>(modal)->button(QMessageBox::No)->click(); });
        check(digest(overlay) == originalDigest && manager.getConfig("ui-original") == saved, name + " cancellation preserves disk and config");
    }
    modalAction(window, "Factory reset", [](QWidget* modal) { qobject_cast<QMessageBox*>(modal)->button(QMessageBox::Yes)->click(); });
    check(waitUntil([&] { return window.isEnabled(); }) && run(io, {"-f", "qcow2", "-c", "read -P 0x00 0 512", overlay}), "confirmed reset clears guest data");
    select(window, "ui-copy");
    const auto clonedDir = manager.getInstanceDir("ui-copy");
    modalAction(window, "Delete", [](QWidget* modal) { qobject_cast<QMessageBox*>(modal)->button(QMessageBox::Yes)->click(); });
    check(waitUntil([&] { return window.isEnabled() && !QFileInfo::exists(clonedDir); }), "confirmed Delete removes only selected instance");
    check(!manager.getConfig("ui-original").isEmpty(), "deletion preserves the other instance");
    window.close();
    QTextStream(stdout) << "Failures: " << failures << Qt::endl;
    return failures ? 1 : 0;
}
