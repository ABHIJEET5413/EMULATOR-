#pragma once

#include <QList>
#include <QString>

#include "keymap_profile.h"

/**
 * @file keymap_editor_model.h
 * @brief Transactional, undoable editor state for one local keymap profile.
 */
class KeymapEditorModel final {
public:
    /** Start an editor transaction from a validated profile. */
    explicit KeymapEditorModel(KeymapProfile profile = {});

    /** Return the current working copy. */
    KeymapProfile profile() const;
    /** Replace the working copy after validating its resolution. */
    bool setProfile(const KeymapProfile& profile, QString* errorMessage = nullptr);
    /** Add or replace a binding only after explicit overwrite approval. */
    bool setBinding(Qt::Key key, const KeyAction& action, bool overwrite, QString* errorMessage = nullptr);
    /** Remove one binding. */
    bool removeBinding(Qt::Key key);
    /** Change the key assigned to an action, rejecting conflicts. */
    bool changeBindingKey(Qt::Key oldKey, Qt::Key newKey, QString* errorMessage = nullptr);
    /** Undo the last accepted edit. */
    bool undo();
    /** Redo the last undone edit. */
    bool redo();
    /** Return whether undo/redo operations are available. */
    bool canUndo() const;
    bool canRedo() const;

private:
    void commit(const KeymapProfile& next);
    KeymapProfile m_profile;
    QList<KeymapProfile> m_undo;
    QList<KeymapProfile> m_redo;
};
