#pragma once

#include <QDialog>
#include <QString>

#include "keymap_editor_model.h"

class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

/**
 * @file keymap_studio_dialog.h
 * @brief Modeless accessible UI for creating, validating, and saving local keymaps.
 */
class KeymapStudioDialog final : public QDialog {
    Q_OBJECT
public:
    /** Edit a profile, saving only when the user explicitly requests it. */
    explicit KeymapStudioDialog(const KeymapProfile& profile, const QString& sourcePath, QWidget* parent = nullptr);
    /** Return the unsaved working profile for viewport marker rendering. */
    KeymapProfile workingProfile() const;

signals:
    /** Changes the host overlay between normal input and editor placement mode. */
    void placementModeChanged(bool enabled);
    /** Supplies current marker data to the editor-only overlay. */
    void workingProfileChanged(const KeymapProfile& profile);
    /** Request a selected action be placed on the guest at profile coordinates. */
    void placementRequested();
    /** A user saved a validated local profile. */
    void profileSaved(const QString& path);

public slots:
    /** Apply a click from the editor overlay to the selected action. */
    void applyPlacement(QPoint profilePoint);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshBindings();
    void loadSelectedBinding();
    KeyAction actionFromFields() const;
    void updateActionFields();
    void showEditorError(const QString& message);
    void saveProfile();
    void importProfile();
    void exportProfile();
    void setPlacementMode(bool enabled);

    KeymapEditorModel m_model;
    QString m_sourcePath;
    QListWidget* m_bindings{};
    QLineEdit* m_profileName{};
    QKeySequenceEdit* m_keyEdit{};
    QComboBox* m_actionType{};
    QSpinBox *m_x{}, *m_y{}, *m_endX{}, *m_endY{}, *m_duration{}, *m_interval{};
    QLabel* m_status{};
    QPushButton *m_addOrUpdate{}, *m_remove{}, *m_undo{}, *m_redo{}, *m_place{};
    bool m_placingSwipeEnd{false};
};
