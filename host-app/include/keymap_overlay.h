#pragma once

#include <QPointer>
#include <QWidget>

#include "keymap_profile.h"

class DisplayEmbed;

/**
 * @file keymap_overlay.h
 * @brief An editor-only marker layer aligned to the letterboxed Android viewport.
 */
class KeymapOverlay final : public QWidget {
    Q_OBJECT
public:
    /** Create a hidden, input-transparent overlay owned by a DisplayEmbed. */
    explicit KeymapOverlay(DisplayEmbed* display);
    /** Show markers and accept placement clicks only while the studio is editing. */
    void setEditing(bool editing);
    /** Render bindings using their reference profile coordinates. */
    void setProfile(const KeymapProfile& profile);
    /** Align the native overlay with the current logical Android rectangle. */
    void syncToViewport();

signals:
    /** User selected a point in profile-reference coordinates. */
    void placementRequested(QPoint profilePoint);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QPointer<DisplayEmbed> m_display;
    KeymapProfile m_profile;
    bool m_editing{false};
};
