#pragma once
#include <QDialog>
#include <QJsonObject>

class QSpinBox;
class QSlider;
class QComboBox;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QToolButton;
/** @file settings_dialog.h
 * Safe, page-based per-instance configuration and local diagnostics UI.
 */
class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    /** Populate settings; port edits are disabled while the VM is running. */
    explicit SettingsDialog(const QJsonObject& config, bool running, QWidget* parent = nullptr);
    /** Return edits merged into the original complete config. */
    QJsonObject getConfig() const;
    /** Check local setting consistency before an instance manager saves it. */
    bool validate(QString* errorMessage = nullptr) const;
private:
    void createGeneralPage();
    void createDisplayPage();
    void createPerformancePage();
    void createControlsPage();
    void createStoragePage();
    void createNetworkPage(bool running);
    void createAdvancedPage();
    void createDiagnosticsPage();
    void setDisplayProfile(const QString& profile);
    void updateDisplayControls();
    void restoreConfigurableDefaults();
    void acceptIfValid();
    void copyDiagnostics();
    void saveDiagnosticsReport();
    void openInstanceFolder();
    void openLogFolder();
    QJsonObject m_originalConfig;
    QListWidget* m_navigation{};
    QStackedWidget* m_pages{};
    QSpinBox *m_cpuCores, *m_qmpPort, *m_adbPort;
    QSlider* m_ramSlider;
    QComboBox *m_displayProfile, *m_resolution, *m_dpi, *m_cameraMode;
    QSpinBox *m_customWidth, *m_customHeight, *m_customDpi;
    QLineEdit *m_vendorId, *m_productId;
    QToolButton *m_phoneProfile{}, *m_tabletProfile{}, *m_desktopProfile{}, *m_fullscreenProfile{}, *m_customProfile{};
};
