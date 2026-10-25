#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QScreen>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "instance_manager.h"
#include "settings_dialog.h"

SettingsDialog::SettingsDialog(const QJsonObject& config, bool running, QWidget* parent)
    : QDialog(parent), m_originalConfig(config)
{
    setWindowTitle("Instance Settings: " + config.value("name").toString());
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    layout->addLayout(form);
    m_cpuCores = new QSpinBox(this);
    m_cpuCores->setRange(1, InstanceManager::maximumCpuCores());
    m_cpuCores->setValue(config.value("cpuCores").toInt());
    form->addRow("CPU cores", m_cpuCores);
    m_ramSlider = new QSlider(Qt::Horizontal, this);
    m_ramSlider->setRange(2, InstanceManager::maximumRamMB() / 512);
    m_ramSlider->setValue(config.value("ramMB").toInt() / 512);
    auto* ramLabel = new QLabel(this);
    const auto showRam = [this, ramLabel] { ramLabel->setText(QString("RAM: %1 MB").arg(m_ramSlider->value() * 512)); };
    connect(m_ramSlider, &QSlider::valueChanged, this, showRam);
    showRam(); form->addRow(ramLabel, m_ramSlider);
    const auto display = config.value("display").toObject();
    m_displayProfile = new QComboBox(this);
    m_displayProfile->addItem("Phone", "phone");
    m_displayProfile->addItem("Tablet", "tablet");
    m_displayProfile->addItem("PC", "pc");
    m_displayProfile->addItem("Fullscreen (host monitor)", "fullscreen");
    m_displayProfile->addItem("Custom", "custom");
    const QString savedProfile = display.value("profile").toString("custom");
    m_displayProfile->setCurrentIndex(qMax(0, m_displayProfile->findData(savedProfile)));
    form->addRow("Display profile", m_displayProfile);
    m_resolution = new QComboBox(this);
    const auto addResolution = [this](const QString& label, const QString& size) {
        m_resolution->addItem(label, size);
    };
    addResolution("Mobile — HD (720 × 1280)", "720x1280");
    addResolution("Mobile — Full HD (1080 × 1920)", "1080x1920");
    addResolution("Mobile — QHD (1440 × 2560)", "1440x2560");
    addResolution("Tablet — WXGA (1280 × 800)", "1280x800");
    addResolution("Tablet — Full HD (1920 × 1200)", "1920x1200");
    addResolution("Tablet — QHD (2560 × 1600)", "2560x1600");
    addResolution("Desktop — Full HD (1920 × 1080)", "1920x1080");
    addResolution("Desktop — QHD (2560 × 1440)", "2560x1440");
    const QString size = QString("%1x%2").arg(display.value("width").toInt()).arg(display.value("height").toInt());
    int resolutionIndex = m_resolution->findData(size);
    if (resolutionIndex < 0) {
        m_resolution->addItem(QString("Custom — %1 × %2").arg(display.value("width").toInt()).arg(display.value("height").toInt()), size);
        resolutionIndex = m_resolution->count() - 1;
    }
    m_resolution->setCurrentIndex(resolutionIndex);
    form->addRow("Resolution", m_resolution);
    m_dpi = new QComboBox(this); m_dpi->addItems({"160", "240", "320", "480"});
    m_dpi->setCurrentText(QString::number(display.value("dpi").toInt()));
    form->addRow("DPI", m_dpi);
    m_customWidth = new QSpinBox(this); m_customHeight = new QSpinBox(this); m_customDpi = new QSpinBox(this);
    for (QSpinBox* field : {m_customWidth, m_customHeight}) field->setRange(320, 4096);
    m_customDpi->setRange(80, 640);
    m_customWidth->setValue(display.value("width").toInt(1080));
    m_customHeight->setValue(display.value("height").toInt(1920));
    m_customDpi->setValue(display.value("dpi").toInt(420));
    form->addRow("Custom width", m_customWidth); form->addRow("Custom height", m_customHeight); form->addRow("Custom DPI", m_customDpi);
    connect(m_resolution, &QComboBox::currentIndexChanged, this, [this] {
        const auto parts = m_resolution->currentData().toString().split('x');
        if (parts.size() == 2) {
            m_customWidth->setValue(parts.at(0).toInt());
            m_customHeight->setValue(parts.at(1).toInt());
        }
    });
    connect(m_dpi, &QComboBox::currentIndexChanged, this, [this] {
        m_customDpi->setValue(m_dpi->currentText().toInt());
    });
    const auto updateProfile = [this] {
        const QString profile = m_displayProfile->currentData().toString();
        const bool custom = profile == "custom";
        for (QSpinBox* field : {m_customWidth, m_customHeight, m_customDpi}) field->setEnabled(custom);
        m_resolution->setEnabled(custom);
        m_dpi->setEnabled(custom);
    };
    connect(m_displayProfile, &QComboBox::currentIndexChanged, this, updateProfile); updateProfile();
    m_qmpPort = new QSpinBox(this); m_adbPort = new QSpinBox(this);
    m_qmpPort->setRange(1024, 65535); m_adbPort->setRange(1024, 65535);
    m_qmpPort->setValue(config.value("qmpPort").toInt()); m_adbPort->setValue(config.value("adbHostPort").toInt());
    m_qmpPort->setEnabled(!running); m_adbPort->setEnabled(!running);
    form->addRow("QMP port", m_qmpPort); form->addRow("ADB port", m_adbPort);
    m_cameraMode = new QComboBox(this);
    m_cameraMode->addItem("None", "none");
    m_cameraMode->addItem("Test pattern (unavailable in bundled QEMU)", "test-pattern");
    qobject_cast<QStandardItemModel*>(m_cameraMode->model())->item(1)->setEnabled(false);
    m_cameraMode->addItem("USB webcam (experimental)", "webcam");
    const auto camera = config.value("camera").toObject();
    m_cameraMode->setCurrentIndex(camera.value("mode").toString() == "webcam" ? 2 : 0);
    form->addRow("Camera", m_cameraMode);
    m_vendorId = new QLineEdit(camera.value("vendorId").toString(), this);
    m_productId = new QLineEdit(camera.value("productId").toString(), this);
    m_vendorId->setPlaceholderText("0x046d"); m_productId->setPlaceholderText("0x082d");
    form->addRow("USB vendor ID", m_vendorId); form->addRow("USB product ID", m_productId);
    const auto updateCamera = [this] {
        const bool enabled = m_cameraMode->currentData().toString() == "webcam";
        m_vendorId->setEnabled(enabled); m_productId->setEnabled(enabled);
    };
    connect(m_cameraMode, &QComboBox::currentIndexChanged, this, updateCamera); updateCamera();
    auto* notice = new QLabel("Changes apply after restart. USB passthrough requires manual driver setup;\n"
                             "Windows camera apps may lose access. See docs/CAMERA.md.", this);
    notice->setWordWrap(true); layout->addWidget(notice);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QJsonObject SettingsDialog::getConfig() const
{
    auto result = m_originalConfig;
    result["cpuCores"] = m_cpuCores->value(); result["ramMB"] = m_ramSlider->value() * 512;
    const QString profile = m_displayProfile->currentData().toString();
    int width = 1080, height = 2400, dpi = 420;
    if (profile == "tablet") { width = 1600; height = 2560; dpi = 280; }
    else if (profile == "pc") { width = 1920; height = 1080; dpi = 160; }
    else if (profile == "fullscreen") {
        const QSize size = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->size() : QSize(1920, 1080);
        width = size.width(); height = size.height(); dpi = 160;
    } else if (profile == "custom") {
        width = m_customWidth->value(); height = m_customHeight->value(); dpi = m_customDpi->value();
    }
    result["display"] = QJsonObject{{"profile", profile}, {"width", width}, {"height", height}, {"dpi", dpi}};
    result["qmpPort"] = m_qmpPort->value(); result["adbHostPort"] = m_adbPort->value();
    auto network = result.value("network").toObject(); network["adbHostPort"] = m_adbPort->value(); result["network"] = network;
    result["camera"] = QJsonObject{{"mode", m_cameraMode->currentData().toString()},
                                  {"vendorId", m_vendorId->text().trimmed()}, {"productId", m_productId->text().trimmed()}};
    return result;
}
