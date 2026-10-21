#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
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
    m_resolution = new QComboBox(this);
    m_resolution->addItems({"720x1280", "1080x1920", "1440x2560", "1280x800", "1920x1200", "2560x1600"});
    const QString size = QString("%1x%2").arg(display.value("width").toInt()).arg(display.value("height").toInt());
    if (m_resolution->findText(size) < 0) m_resolution->addItem(size);
    m_resolution->setCurrentText(size);
    form->addRow("Resolution", m_resolution);
    m_dpi = new QComboBox(this); m_dpi->addItems({"160", "240", "320", "480"});
    m_dpi->setCurrentText(QString::number(display.value("dpi").toInt()));
    form->addRow("DPI", m_dpi);
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
    const auto parts = m_resolution->currentText().split('x');
    result["display"] = QJsonObject{{"width", parts.value(0).toInt()}, {"height", parts.value(1).toInt()}, {"dpi", m_dpi->currentText().toInt()}};
    result["qmpPort"] = m_qmpPort->value(); result["adbHostPort"] = m_adbPort->value();
    auto network = result.value("network").toObject(); network["adbHostPort"] = m_adbPort->value(); result["network"] = network;
    result["camera"] = QJsonObject{{"mode", m_cameraMode->currentData().toString()},
                                  {"vendorId", m_vendorId->text().trimmed()}, {"productId", m_productId->text().trimmed()}};
    return result;
}
