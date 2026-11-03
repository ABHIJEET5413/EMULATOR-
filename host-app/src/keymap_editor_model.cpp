#include "keymap_editor_model.h"

namespace {
void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) *errorMessage = message;
}

bool validProfile(const KeymapProfile& profile, QString* errorMessage)
{
    if (profile.resolution.width() < 1 || profile.resolution.height() < 1) {
        setError(errorMessage, QStringLiteral("A keymap needs a positive reference resolution."));
        return false;
    }
    return true;
}
} // namespace

KeymapEditorModel::KeymapEditorModel(KeymapProfile profile)
    : m_profile(std::move(profile))
{
    if (m_profile.resolution.width() < 1 || m_profile.resolution.height() < 1) m_profile.resolution = {1080, 1920};
}

KeymapProfile KeymapEditorModel::profile() const { return m_profile; }

bool KeymapEditorModel::setProfile(const KeymapProfile& profile, QString* errorMessage)
{
    if (!validProfile(profile, errorMessage)) return false;
    commit(profile);
    return true;
}

bool KeymapEditorModel::setBinding(const Qt::Key key, const KeyAction& action, const bool overwrite, QString* errorMessage)
{
    if (key == Qt::Key_unknown) { setError(errorMessage, QStringLiteral("Choose a keyboard key before saving a binding.")); return false; }
    if (m_profile.bindings.contains(key) && !overwrite) {
        setError(errorMessage, QStringLiteral("That key already has a binding. Replace it or choose another key."));
        return false;
    }
    KeymapProfile next = m_profile;
    next.bindings.insert(key, action);
    commit(next);
    return true;
}

bool KeymapEditorModel::removeBinding(const Qt::Key key)
{
    if (!m_profile.bindings.contains(key)) return false;
    KeymapProfile next = m_profile;
    next.bindings.remove(key);
    commit(next);
    return true;
}

bool KeymapEditorModel::changeBindingKey(const Qt::Key oldKey, const Qt::Key newKey, QString* errorMessage)
{
    if (!m_profile.bindings.contains(oldKey)) { setError(errorMessage, QStringLiteral("The selected binding no longer exists.")); return false; }
    if (newKey == Qt::Key_unknown) { setError(errorMessage, QStringLiteral("Choose a valid keyboard key.")); return false; }
    if (newKey != oldKey && m_profile.bindings.contains(newKey)) {
        setError(errorMessage, QStringLiteral("That key is already assigned to another action."));
        return false;
    }
    KeymapProfile next = m_profile;
    const KeyAction action = next.bindings.take(oldKey);
    next.bindings.insert(newKey, action);
    commit(next);
    return true;
}

bool KeymapEditorModel::undo()
{
    if (m_undo.isEmpty()) return false;
    m_redo.append(m_profile); m_profile = m_undo.takeLast(); return true;
}

bool KeymapEditorModel::redo()
{
    if (m_redo.isEmpty()) return false;
    m_undo.append(m_profile); m_profile = m_redo.takeLast(); return true;
}

bool KeymapEditorModel::canUndo() const { return !m_undo.isEmpty(); }
bool KeymapEditorModel::canRedo() const { return !m_redo.isEmpty(); }

void KeymapEditorModel::commit(const KeymapProfile& next)
{
    m_undo.append(m_profile); m_redo.clear(); m_profile = next;
}
