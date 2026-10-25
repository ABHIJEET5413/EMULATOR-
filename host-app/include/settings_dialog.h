#pragma once
#include <QDialog>
#include <QJsonObject>

class QSpinBox;
class QSlider;
class QComboBox;
class QLineEdit;
/** @file settings_dialog.h
 * Validated per-instance performance, display, port, and USB camera settings.
 */
class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    /** Populate settings; port edits are disabled while the VM is running. */
    explicit SettingsDialog(const QJsonObject& config, bool running, QWidget* parent = nullptr);
    /** Return edits merged into the original complete config. */
    QJsonObject getConfig() const;
private:
    QJsonObject m_originalConfig;
    QSpinBox *m_cpuCores, *m_qmpPort, *m_adbPort;
    QSlider* m_ramSlider;
    QComboBox *m_displayProfile, *m_resolution, *m_dpi, *m_cameraMode;
    QSpinBox *m_customWidth, *m_customHeight, *m_customDpi;
    QLineEdit *m_vendorId, *m_productId;
};
