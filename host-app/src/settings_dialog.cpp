#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QToolButton>
#include <QVBoxLayout>

#include "diagnostics_service.h"
#include "instance_manager.h"
#include "settings_dialog.h"
#include "ui_theme.h"

namespace {
QWidget* createPage(QStackedWidget* pages)
{
    auto* page = new QWidget(pages);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(10);
    pages->addWidget(page);
    return page;
}

QGroupBox* createGroup(const QString& title, QWidget* parent)
{
    auto* group = new QGroupBox(title, parent);
    group->setObjectName(QStringLiteral("sabiSettingsGroup"));
    group->setLayout(new QFormLayout(group));
    return group;
}

QLabel* createNotice(const QString& text, QWidget* parent)
{
    auto* notice = new QLabel(text, parent);
    notice->setObjectName(QStringLiteral("sabiSettingsNotice"));
    notice->setWordWrap(true);
    return notice;
}

QString fileName(const QJsonObject& config, const QString& key)
{
    const QString path = config.value(key).toString();
    return path.isEmpty() ? QStringLiteral("Not configured") : QFileInfo(path).fileName();
}
} // namespace

SettingsDialog::SettingsDialog(const QJsonObject& config, const bool running, QWidget* parent)
    : QDialog(parent), m_originalConfig(config)
{
    UiTheme::polishWindow(this, QStringLiteral("sabiSettingsDialog"));
    setWindowTitle(QStringLiteral("Sabi Control Center — %1").arg(config.value("name").toString()));
    setMinimumSize(760, 520);
    auto* root = new QVBoxLayout(this);
    auto* title = new QLabel(QStringLiteral("Instance control center"), this);
    title->setObjectName(QStringLiteral("sabiSettingsTitle"));
    root->addWidget(title);
    root->addWidget(createNotice(running
        ? QStringLiteral("This instance is running. Network ports are locked until it stops. Most changes apply on the next start.")
        : QStringLiteral("Settings are per instance. CPU, RAM, display, camera, and ports apply on the next start."), this));
    auto* content = new QHBoxLayout;
    m_navigation = new QListWidget(this);
    m_navigation->setObjectName(QStringLiteral("sabiSettingsNav"));
    m_navigation->setAccessibleName(QStringLiteral("Control center sections"));
    m_navigation->setFixedWidth(170);
    for (const QString& label : {QStringLiteral("General"), QStringLiteral("Display"), QStringLiteral("Performance"),
                                 QStringLiteral("Controls"), QStringLiteral("Storage"), QStringLiteral("Network / ADB"),
                                 QStringLiteral("Advanced"), QStringLiteral("Diagnostics")}) m_navigation->addItem(label);
    m_pages = new QStackedWidget(this);
    m_pages->setAccessibleName(QStringLiteral("Control center page"));
    content->addWidget(m_navigation);
    content->addWidget(m_pages, 1);
    root->addLayout(content, 1);
    createGeneralPage(); createDisplayPage(); createPerformancePage(); createControlsPage();
    createStoragePage(); createNetworkPage(running); createAdvancedPage(); createDiagnosticsPage();
    connect(m_navigation, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_navigation->setCurrentRow(0);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* restore = buttons->addButton(QStringLiteral("Restore configurable defaults"), QDialogButtonBox::ResetRole);
    restore->setToolTip(QStringLiteral("Restores CPU, RAM, display, and camera settings without changing name, disk paths, or ports."));
    connect(restore, &QPushButton::clicked, this, &SettingsDialog::restoreConfigurableDefaults);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::acceptIfValid);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void SettingsDialog::createGeneralPage()
{
    QWidget* page = createPage(m_pages);
    auto* group = createGroup(QStringLiteral("Instance identity"), page);
    auto* form = qobject_cast<QFormLayout*>(group->layout());
    form->addRow(QStringLiteral("Name"), new QLabel(m_originalConfig.value("name").toString(), group));
    form->addRow(QStringLiteral("Scope"), new QLabel(QStringLiteral("This instance only"), group));
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice(
        QStringLiteral("To rename, clone, reset, or delete an instance, use the Instances window. Those operations affect its overlay disk."), page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createDisplayPage()
{
    QWidget* page = createPage(m_pages);
    auto* group = createGroup(QStringLiteral("Display profile"), page);
    auto* form = qobject_cast<QFormLayout*>(group->layout());
    auto* cards = new QHBoxLayout;
    const auto addCard = [this, cards, group](QToolButton*& button, const QString& name, const QString& label, const QString& profile) {
        button = new QToolButton(group); button->setObjectName(name); button->setText(label); button->setCheckable(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly); button->setAccessibleName(QStringLiteral("Use %1 display profile").arg(label));
        button->setProperty("profile", profile); cards->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, profile] { setDisplayProfile(profile); });
    };
    addCard(m_phoneProfile, "settingsProfilePhone", "Phone", "phone");
    addCard(m_tabletProfile, "settingsProfileTablet", "Tablet", "tablet");
    addCard(m_desktopProfile, "settingsProfileDesktop", "Desktop", "pc");
    addCard(m_fullscreenProfile, "settingsProfileFullscreen", "Fullscreen", "fullscreen");
    addCard(m_customProfile, "settingsProfileCustom", "Custom", "custom");
    form->addRow(cards);
    const QJsonObject display = m_originalConfig.value("display").toObject();
    m_displayProfile = new QComboBox(group);
    m_displayProfile->addItem("Phone", "phone"); m_displayProfile->addItem("Tablet", "tablet");
    m_displayProfile->addItem("Desktop", "pc"); m_displayProfile->addItem("Fullscreen (host monitor)", "fullscreen");
    m_displayProfile->addItem("Custom", "custom");
    m_displayProfile->setCurrentIndex(qMax(0, m_displayProfile->findData(display.value("profile").toString("custom"))));
    form->addRow("Display profile", m_displayProfile);
    m_resolution = new QComboBox(group);
    const auto addResolution = [this](const QString& label, const QString& size) { m_resolution->addItem(label, size); };
    addResolution("Mobile — HD (720 × 1280)", "720x1280"); addResolution("Mobile — Full HD (1080 × 1920)", "1080x1920");
    addResolution("Mobile — QHD (1440 × 2560)", "1440x2560"); addResolution("Tablet — WXGA (1280 × 800)", "1280x800");
    addResolution("Tablet — Full HD (1920 × 1200)", "1920x1200"); addResolution("Tablet — QHD (2560 × 1600)", "2560x1600");
    addResolution("Desktop — Full HD (1920 × 1080)", "1920x1080"); addResolution("Desktop — QHD (2560 × 1440)", "2560x1440");
    const QString size = QStringLiteral("%1x%2").arg(display.value("width").toInt()).arg(display.value("height").toInt());
    int resolution = m_resolution->findData(size);
    if (resolution < 0) { m_resolution->addItem(QStringLiteral("Custom — %1").arg(size), size); resolution = m_resolution->count() - 1; }
    m_resolution->setCurrentIndex(resolution); form->addRow("Resolution", m_resolution);
    m_dpi = new QComboBox(group); m_dpi->addItems({"160", "240", "320", "420", "480"});
    m_dpi->setCurrentText(QString::number(display.value("dpi").toInt(420))); form->addRow("DPI", m_dpi);
    m_customWidth = new QSpinBox(group); m_customHeight = new QSpinBox(group); m_customDpi = new QSpinBox(group);
    for (QSpinBox* field : {m_customWidth, m_customHeight}) field->setRange(320, 4096);
    m_customDpi->setRange(80, 640);
    m_customWidth->setValue(display.value("width").toInt(1080)); m_customHeight->setValue(display.value("height").toInt(1920)); m_customDpi->setValue(display.value("dpi").toInt(420));
    form->addRow("Custom width", m_customWidth); form->addRow("Custom height", m_customHeight); form->addRow("Custom DPI", m_customDpi);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice(
        QStringLiteral("Android receives size and density changes after it is ready. QEMU's initial raster size changes on the next VM start."), page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
    connect(m_displayProfile, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsDialog::updateDisplayControls);
    connect(m_resolution, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const QStringList parts = m_resolution->currentData().toString().split('x');
        if (parts.size() == 2) { m_customWidth->setValue(parts.at(0).toInt()); m_customHeight->setValue(parts.at(1).toInt()); }
        setDisplayProfile("custom");
    });
    connect(m_dpi, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_customDpi->setValue(m_dpi->currentText().toInt());
        setDisplayProfile("custom");
    });
    updateDisplayControls();
}

void SettingsDialog::createPerformancePage()
{
    QWidget* page = createPage(m_pages); auto* group = createGroup("Resource policy", page); auto* form = qobject_cast<QFormLayout*>(group->layout());
    m_cpuCores = new QSpinBox(group); m_cpuCores->setRange(1, InstanceManager::maximumCpuCores()); m_cpuCores->setValue(m_originalConfig.value("cpuCores").toInt(1)); form->addRow("CPU cores", m_cpuCores);
    m_ramSlider = new QSlider(Qt::Horizontal, group); m_ramSlider->setRange(2, InstanceManager::maximumRamMB() / 512);
    m_ramSlider->setValue(qBound(m_ramSlider->minimum(), m_originalConfig.value("ramMB").toInt(2048) / 512, m_ramSlider->maximum()));
    auto* ramLabel = new QLabel(group); connect(m_ramSlider, &QSlider::valueChanged, this, [this, ramLabel] { ramLabel->setText(QStringLiteral("RAM: %1 MB").arg(m_ramSlider->value() * 512)); });
    ramLabel->setText(QStringLiteral("RAM: %1 MB").arg(m_ramSlider->value() * 512)); form->addRow(ramLabel, m_ramSlider);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice("These are per-instance QEMU launch limits, not an FPS or eco control. Changes require a restart.", page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createControlsPage()
{
    QWidget* page = createPage(m_pages);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice("Keyboard mappings use saved JSON keymap profiles. A visual keymap editor is planned for Phase 10; no unimplemented edit control is shown here.", page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createStoragePage()
{
    QWidget* page = createPage(m_pages); auto* group = createGroup("Overlay storage", page); auto* form = qobject_cast<QFormLayout*>(group->layout());
    form->addRow("Base image", new QLabel(fileName(m_originalConfig, "baseImage"), group)); form->addRow("Writable overlay", new QLabel(fileName(m_originalConfig, "overlayImage"), group));
    auto* openFolder = new QToolButton(group); openFolder->setText("Open instance folder"); openFolder->setToolButtonStyle(Qt::ToolButtonTextOnly); connect(openFolder, &QToolButton::clicked, this, &SettingsDialog::openInstanceFolder); form->addRow(openFolder);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice("Factory reset and deletion remain in the Instances window, where destructive operations require confirmation.", page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createNetworkPage(const bool running)
{
    QWidget* page = createPage(m_pages); auto* group = createGroup("Local VM ports", page); auto* form = qobject_cast<QFormLayout*>(group->layout());
    m_qmpPort = new QSpinBox(group); m_adbPort = new QSpinBox(group); m_qmpPort->setRange(1024, 65535); m_adbPort->setRange(1024, 65535);
    m_qmpPort->setValue(m_originalConfig.value("qmpPort").toInt()); m_adbPort->setValue(m_originalConfig.value("adbHostPort").toInt());
    m_qmpPort->setEnabled(!running); m_adbPort->setEnabled(!running); form->addRow("QMP port", m_qmpPort); form->addRow("ADB port", m_adbPort);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice(running ? "Stop the instance before changing ports." : "Sabi checks port conflicts when saving. Both values must be distinct and available.", page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createAdvancedPage()
{
    QWidget* page = createPage(m_pages); auto* group = createGroup("Experimental camera", page); auto* form = qobject_cast<QFormLayout*>(group->layout()); const QJsonObject camera = m_originalConfig.value("camera").toObject();
    m_cameraMode = new QComboBox(group); m_cameraMode->addItem("None", "none"); m_cameraMode->addItem("Test pattern (unavailable in bundled QEMU)", "test-pattern");
    qobject_cast<QStandardItemModel*>(m_cameraMode->model())->item(1)->setEnabled(false); m_cameraMode->addItem("USB webcam (experimental)", "webcam");
    m_cameraMode->setCurrentIndex(camera.value("mode").toString() == "webcam" ? 2 : 0); form->addRow("Camera", m_cameraMode);
    m_vendorId = new QLineEdit(camera.value("vendorId").toString(), group); m_productId = new QLineEdit(camera.value("productId").toString(), group); m_vendorId->setPlaceholderText("0x046d"); m_productId->setPlaceholderText("0x082d");
    form->addRow("USB vendor ID", m_vendorId); form->addRow("USB product ID", m_productId);
    connect(m_cameraMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { const bool webcam = m_cameraMode->currentData().toString() == "webcam"; m_vendorId->setEnabled(webcam); m_productId->setEnabled(webcam); });
    m_vendorId->setEnabled(m_cameraMode->currentData().toString() == "webcam"); m_productId->setEnabled(m_vendorId->isEnabled());
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(group);
    qobject_cast<QVBoxLayout*>(page->layout())->addWidget(createNotice("USB camera passthrough requires manual driver setup and can make the camera unavailable to Windows apps. See docs/CAMERA.md. Changes require a restart.", page));
    qobject_cast<QVBoxLayout*>(page->layout())->addStretch();
}

void SettingsDialog::createDiagnosticsPage()
{
    QWidget* page = createPage(m_pages); auto* layout = qobject_cast<QVBoxLayout*>(page->layout());
    layout->addWidget(createNotice("Diagnostics stay on this computer. A summary contains configuration metadata only: no ADB keys, APKs, guest files, full host paths, or log contents.", page));
    auto* buttons = new QHBoxLayout;
    auto* openLogs = new QToolButton(page); openLogs->setText("Open local logs"); openLogs->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* copySummary = new QToolButton(page); copySummary->setText("Copy safe summary"); copySummary->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto* saveReport = new QToolButton(page); saveReport->setText("Save support report…"); saveReport->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(openLogs, &QToolButton::clicked, this, &SettingsDialog::openLogFolder);
    connect(copySummary, &QToolButton::clicked, this, &SettingsDialog::copyDiagnostics);
    connect(saveReport, &QToolButton::clicked, this, &SettingsDialog::saveDiagnosticsReport);
    buttons->addWidget(openLogs); buttons->addWidget(copySummary); buttons->addWidget(saveReport);
    layout->addLayout(buttons); layout->addStretch();
}

void SettingsDialog::setDisplayProfile(const QString& profile)
{
    const int index = m_displayProfile->findData(profile);
    if (index >= 0 && index != m_displayProfile->currentIndex()) m_displayProfile->setCurrentIndex(index);
    updateDisplayControls();
}

void SettingsDialog::updateDisplayControls()
{
    const QString profile = m_displayProfile->currentData().toString(); const bool custom = profile == "custom";
    for (QSpinBox* field : {m_customWidth, m_customHeight, m_customDpi}) field->setEnabled(custom);
    for (QToolButton* button : {m_phoneProfile, m_tabletProfile, m_desktopProfile, m_fullscreenProfile, m_customProfile}) button->setChecked(button->property("profile").toString() == profile);
}

void SettingsDialog::restoreConfigurableDefaults()
{
    m_cpuCores->setValue(1); m_ramSlider->setValue(qBound(m_ramSlider->minimum(), 4, m_ramSlider->maximum()));
    m_customWidth->setValue(1080); m_customHeight->setValue(2400); m_customDpi->setValue(420); setDisplayProfile("phone");
    m_cameraMode->setCurrentIndex(m_cameraMode->findData("none")); m_vendorId->clear(); m_productId->clear();
}

bool SettingsDialog::validate(QString* errorMessage) const
{
    if (m_qmpPort->value() == m_adbPort->value()) { if (errorMessage) *errorMessage = "QMP and ADB must use different local ports."; return false; }
    return true;
}

void SettingsDialog::acceptIfValid()
{
    QString error; if (!validate(&error)) { QMessageBox::warning(this, "Settings not saved", error); return; } accept();
}

void SettingsDialog::copyDiagnostics()
{
    QApplication::clipboard()->setText(DiagnosticsService::redactedSummary(getConfig()));
    QMessageBox::information(this, "Diagnostics copied", "A redacted local summary was copied to the clipboard.");
}

void SettingsDialog::saveDiagnosticsReport()
{
    const QString path = QFileDialog::getSaveFileName(this, "Save redacted support report", "sabi-support-report.txt", "Text files (*.txt)"); if (path.isEmpty()) return;
    QString error; if (!DiagnosticsService::saveRedactedReport(path, getConfig(), &error)) QMessageBox::warning(this, "Report not saved", error);
}

void SettingsDialog::openInstanceFolder()
{
    const QFileInfo overlay(m_originalConfig.value("overlayImage").toString());
    if (overlay.filePath().isEmpty() || !overlay.dir().exists() || !QDesktopServices::openUrl(QUrl::fromLocalFile(overlay.absolutePath()))) QMessageBox::warning(this, "Folder unavailable", "The current instance folder could not be opened.");
}

void SettingsDialog::openLogFolder()
{
    QString error; if (!DiagnosticsService::openLogDirectory(&error)) QMessageBox::warning(this, "Logs unavailable", error);
}

QJsonObject SettingsDialog::getConfig() const
{
    QJsonObject result = m_originalConfig; result["cpuCores"] = m_cpuCores->value(); result["ramMB"] = m_ramSlider->value() * 512;
    const QString profile = m_displayProfile->currentData().toString(); int width = 1080, height = 2400, dpi = 420;
    if (profile == "tablet") { width = 1600; height = 2560; dpi = 280; } else if (profile == "pc") { width = 1920; height = 1080; dpi = 160; }
    else if (profile == "fullscreen") { const QSize size = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->size() : QSize(1920, 1080); width = size.width(); height = size.height(); dpi = 160; }
    else if (profile == "custom") { width = m_customWidth->value(); height = m_customHeight->value(); dpi = m_customDpi->value(); }
    result["display"] = QJsonObject{{"profile", profile}, {"width", width}, {"height", height}, {"dpi", dpi}};
    result["qmpPort"] = m_qmpPort->value(); result["adbHostPort"] = m_adbPort->value(); QJsonObject network = result.value("network").toObject(); network["adbHostPort"] = m_adbPort->value(); result["network"] = network;
    result["camera"] = QJsonObject{{"mode", m_cameraMode->currentData().toString()}, {"vendorId", m_vendorId->text().trimmed()}, {"productId", m_productId->text().trimmed()}};
    return result;
}
