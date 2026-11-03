#include <QAbstractButton>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "keymap_studio_dialog.h"
#include "gamepad_probe.h"
#include "ui_theme.h"

namespace {
QString keyText(const Qt::Key key) { return QKeySequence(key).toString(QKeySequence::NativeText); }
QString actionText(const KeyAction::Type type)
{
    switch (type) { case KeyAction::Tap: return "Tap"; case KeyAction::Hold: return "Hold"; case KeyAction::Swipe: return "Swipe"; case KeyAction::Passthrough: return "Passthrough"; case KeyAction::RepeatTap: return "Repeat tap"; }
    return {};
}
} // namespace

KeymapStudioDialog::KeymapStudioDialog(const KeymapProfile& profile, const QString& sourcePath, QWidget* parent)
    : QDialog(parent), m_model(profile), m_sourcePath(sourcePath)
{
    UiTheme::polishWindow(this, QStringLiteral("sabiKeymapStudio"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false); setWindowTitle(QStringLiteral("Sabi Controls Studio")); setMinimumSize(610, 520);
    auto* root = new QVBoxLayout(this);
    auto* heading = new QLabel(QStringLiteral("Controls Studio"), this); heading->setObjectName(QStringLiteral("sabiSettingsTitle")); root->addWidget(heading);
    auto* notice = new QLabel(QStringLiteral("Edit mode places only the selected action. Guest input is released while placing markers and resumes when edit mode ends."), this);
    notice->setObjectName(QStringLiteral("sabiSettingsNotice")); notice->setWordWrap(true); root->addWidget(notice);
    auto* profileForm = new QFormLayout;
    m_profileName = new QLineEdit(profile.name, this); m_profileName->setAccessibleName("Keymap profile name"); profileForm->addRow("Profile name", m_profileName);
    root->addLayout(profileForm);
    auto* columns = new QHBoxLayout; m_bindings = new QListWidget(this); m_bindings->setAccessibleName("Saved bindings"); columns->addWidget(m_bindings, 1);
    auto* fields = new QFormLayout;
    m_keyEdit = new QKeySequenceEdit(this); m_keyEdit->setMaximumSequenceLength(1); fields->addRow("Keyboard key", m_keyEdit);
    m_actionType = new QComboBox(this);
    m_actionType->addItem("Tap", KeyAction::Tap); m_actionType->addItem("Hold", KeyAction::Hold); m_actionType->addItem("Swipe", KeyAction::Swipe);
    m_actionType->addItem("Passthrough", KeyAction::Passthrough); m_actionType->addItem("Repeat tap", KeyAction::RepeatTap); fields->addRow("Action", m_actionType);
    const auto addCoordinate = [this, fields](const QString& label, QSpinBox*& box) { box = new QSpinBox(this); box->setRange(0, 8192); fields->addRow(label, box); };
    addCoordinate("X", m_x); addCoordinate("Y", m_y); addCoordinate("Swipe end X", m_endX); addCoordinate("Swipe end Y", m_endY);
    m_duration = new QSpinBox(this); m_duration->setRange(1, 10000); m_duration->setValue(120); fields->addRow("Swipe duration (ms)", m_duration);
    m_interval = new QSpinBox(this); m_interval->setRange(1, 10000); m_interval->setValue(100); fields->addRow("Repeat interval (ms)", m_interval);
    columns->addLayout(fields, 2); root->addLayout(columns, 1);
    auto* editButtons = new QHBoxLayout;
    m_addOrUpdate = new QPushButton("Add binding", this); m_remove = new QPushButton("Remove", this); m_undo = new QPushButton("Undo", this); m_redo = new QPushButton("Redo", this); m_place = new QPushButton("Place on Android", this);
    for (auto* button : {m_addOrUpdate, m_remove, m_undo, m_redo, m_place}) editButtons->addWidget(button); root->addLayout(editButtons);
    m_status = new QLabel(this); m_status->setWordWrap(true); root->addWidget(m_status);
    auto* gamepadStatus = new QLabel(GamepadProbe::capabilitySummary(), this);
    gamepadStatus->setObjectName(QStringLiteral("sabiSettingsNotice")); gamepadStatus->setWordWrap(true);
    root->addWidget(gamepadStatus);
    auto* files = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    files->addButton("Import…", QDialogButtonBox::ActionRole); files->addButton("Export…", QDialogButtonBox::ActionRole);
    root->addWidget(files);
    connect(m_bindings, &QListWidget::currentRowChanged, this, [this] { loadSelectedBinding(); });
    connect(m_actionType, qOverload<int>(&QComboBox::currentIndexChanged), this, &KeymapStudioDialog::updateActionFields);
    connect(m_addOrUpdate, &QPushButton::clicked, this, [this] {
        const QKeySequence sequence = m_keyEdit->keySequence(); const Qt::Key key = sequence.count() == 1 ? static_cast<Qt::Key>(sequence[0].key()) : Qt::Key_unknown;
        QString error; const bool exists = m_model.profile().bindings.contains(key);
        if (exists && QMessageBox::question(this, "Replace binding?", "This key already has a binding. Replace it?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        if (!m_model.setBinding(key, actionFromFields(), exists, &error)) { showEditorError(error); return; }
        refreshBindings(); emit workingProfileChanged(m_model.profile());
    });
    connect(m_remove, &QPushButton::clicked, this, [this] { const auto item = m_bindings->currentItem(); if (item && m_model.removeBinding(static_cast<Qt::Key>(item->data(Qt::UserRole).toInt()))) { refreshBindings(); emit workingProfileChanged(m_model.profile()); } });
    connect(m_undo, &QPushButton::clicked, this, [this] { if (m_model.undo()) { refreshBindings(); emit workingProfileChanged(m_model.profile()); } });
    connect(m_redo, &QPushButton::clicked, this, [this] { if (m_model.redo()) { refreshBindings(); emit workingProfileChanged(m_model.profile()); } });
    connect(m_place, &QPushButton::clicked, this, [this] { if (!m_bindings->currentItem()) { showEditorError("Select or add a binding before placing it."); return; } m_placingSwipeEnd = false; setPlacementMode(true); emit placementRequested(); });
    const auto buttons = files->buttons();
    for (QAbstractButton* button : buttons) {
        if (button->text() == "Save") connect(button, &QAbstractButton::clicked, this, &KeymapStudioDialog::saveProfile);
        else if (button->text() == "Close") connect(button, &QAbstractButton::clicked, this, &QDialog::close);
        else if (button->text().startsWith("Import")) connect(button, &QAbstractButton::clicked, this, &KeymapStudioDialog::importProfile);
        else if (button->text().startsWith("Export")) connect(button, &QAbstractButton::clicked, this, &KeymapStudioDialog::exportProfile);
    }
    refreshBindings(); updateActionFields();
}

KeymapProfile KeymapStudioDialog::workingProfile() const { return m_model.profile(); }

void KeymapStudioDialog::applyPlacement(const QPoint point)
{
    auto* item = m_bindings->currentItem(); if (!item) return;
    const Qt::Key key = static_cast<Qt::Key>(item->data(Qt::UserRole).toInt());
    auto profile = m_model.profile(); KeyAction action = profile.bindings.value(key);
    if (action.type == KeyAction::Swipe && !m_placingSwipeEnd) { action.startX = point.x(); action.startY = point.y(); m_placingSwipeEnd = true; m_status->setText("Swipe start placed. Click the Android viewport again to place the end."); return; }
    if (action.type == KeyAction::Swipe) { action.endX = point.x(); action.endY = point.y(); } else { action.x = point.x(); action.y = point.y(); }
    QString error; m_model.setBinding(key, action, true, &error); setPlacementMode(false); refreshBindings(); emit workingProfileChanged(m_model.profile());
}

void KeymapStudioDialog::closeEvent(QCloseEvent* event) { setPlacementMode(false); QDialog::closeEvent(event); }

void KeymapStudioDialog::refreshBindings()
{
    const Qt::Key selected = m_bindings->currentItem() ? static_cast<Qt::Key>(m_bindings->currentItem()->data(Qt::UserRole).toInt()) : Qt::Key_unknown;
    m_bindings->clear(); const auto profile = m_model.profile();
    for (auto it = profile.bindings.cbegin(); it != profile.bindings.cend(); ++it) { auto* item = new QListWidgetItem(QStringLiteral("%1 — %2").arg(keyText(it.key()), actionText(it.value().type)), m_bindings); item->setData(Qt::UserRole, static_cast<int>(it.key())); if (it.key() == selected) m_bindings->setCurrentItem(item); }
    m_undo->setEnabled(m_model.canUndo()); m_redo->setEnabled(m_model.canRedo()); m_remove->setEnabled(m_bindings->count() > 0);
    m_status->setText(QStringLiteral("%1 binding(s). All changes are local until Save is selected.").arg(m_bindings->count()));
}

void KeymapStudioDialog::loadSelectedBinding()
{
    const auto item = m_bindings->currentItem(); if (!item) return; const auto profile = m_model.profile(); const Qt::Key key = static_cast<Qt::Key>(item->data(Qt::UserRole).toInt()); const KeyAction action = profile.bindings.value(key);
    m_keyEdit->setKeySequence(QKeySequence(key)); m_actionType->setCurrentIndex(m_actionType->findData(action.type));
    m_x->setValue(action.x); m_y->setValue(action.y); m_endX->setValue(action.endX); m_endY->setValue(action.endY); m_duration->setValue(action.durationMs); m_interval->setValue(action.intervalMs); updateActionFields();
}

KeyAction KeymapStudioDialog::actionFromFields() const
{
    KeyAction action; action.type = static_cast<KeyAction::Type>(m_actionType->currentData().toInt()); action.x = m_x->value(); action.y = m_y->value(); action.startX = m_x->value(); action.startY = m_y->value(); action.endX = m_endX->value(); action.endY = m_endY->value(); action.durationMs = m_duration->value(); action.intervalMs = m_interval->value(); return action;
}

void KeymapStudioDialog::updateActionFields()
{
    const auto type = static_cast<KeyAction::Type>(m_actionType->currentData().toInt()); const bool point = type != KeyAction::Passthrough; const bool swipe = type == KeyAction::Swipe;
    m_x->setEnabled(point); m_y->setEnabled(point); m_endX->setEnabled(swipe); m_endY->setEnabled(swipe); m_duration->setEnabled(swipe); m_interval->setEnabled(type == KeyAction::RepeatTap);
}

void KeymapStudioDialog::showEditorError(const QString& message) { QMessageBox::warning(this, "Controls Studio", message); }

void KeymapStudioDialog::saveProfile()
{
    KeymapProfile profile = m_model.profile();
    profile.name = m_profileName->text().trimmed();
    if (profile.name.isEmpty()) { showEditorError("Give this keymap a name before saving it."); return; }
    QString metadataError;
    if (!m_model.setProfile(profile, &metadataError)) { showEditorError(metadataError); return; }
    const QString defaultDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("keymaps");
    const QString path = m_sourcePath.isEmpty() ? QFileDialog::getSaveFileName(this, "Save keymap", QDir(defaultDirectory).filePath("custom.json"), "Keymap profiles (*.json)") : m_sourcePath;
    QString error; if (path.isEmpty()) return; if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !profile.saveToFile(path, &error)) { showEditorError(error.isEmpty() ? "Could not create the local keymap folder." : error); return; } m_sourcePath = path; emit profileSaved(path); m_status->setText(QStringLiteral("Saved local keymap: %1").arg(QFileInfo(path).fileName()));
}

void KeymapStudioDialog::importProfile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Import keymap", QString(), "Keymap profiles (*.json)"); if (path.isEmpty()) return; QString error; const auto profile = KeymapProfile::loadFromFile(path, &error); if (!profile || !m_model.setProfile(*profile, &error)) { showEditorError(error); return; } m_profileName->setText(profile->name); m_sourcePath.clear(); refreshBindings(); emit workingProfileChanged(m_model.profile());
}

void KeymapStudioDialog::exportProfile()
{
    const QString path = QFileDialog::getSaveFileName(this, "Export keymap", "sabi-keymap.json", "Keymap profiles (*.json)"); if (path.isEmpty()) return; QString error; if (!m_model.profile().saveToFile(path, &error)) showEditorError(error);
}

void KeymapStudioDialog::setPlacementMode(const bool enabled)
{
    m_place->setText(enabled ? "Placing on Android…" : "Place on Android"); emit placementModeChanged(enabled);
}
