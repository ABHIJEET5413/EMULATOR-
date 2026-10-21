#pragma once

#include <QMap>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <Qt>

#include <optional>

#include "keymap_profile.h"

class QTimer;

/**
 * @file keymap_engine.h
 * @brief Executes keymap actions and scales profile coordinates for the guest.
 */
class KeymapEngine final : public QObject {
    Q_OBJECT

public:
    /** Create an engine; keymaps start disabled until explicitly enabled. */
    explicit KeymapEngine(QObject* parent = nullptr);

    /** Load a profile from JSON, keeping the current profile on failure. */
    bool loadProfile(const QString& path, QString* errorMessage = nullptr);

    /** Enable or disable key mapping. Disabling releases active actions. */
    void setEnabled(bool enabled);

    /** Return whether key mapping is enabled. */
    bool isEnabled() const;

    /** Handle a key press; return true when the profile consumes the key. */
    bool handleKeyPress(Qt::Key key);

    /** Handle a key release and stop actions started by that key. */
    bool handleKeyRelease(Qt::Key key);

    /** Release all active keys and stop their timers. */
    void releaseAll();

    /** List JSON profile paths in a directory, sorted by name. */
    static QStringList listProfiles(const QString& directory);

    /** Set the current guest resolution; an invalid size uses profile size. */
    void setGuestResolution(QSize resolution);

    /** Return guest size, or profile size when guest size has not been set. */
    QSize guestResolution() const;

    /** Return a copy of the active profile, if one is loaded. */
    std::optional<KeymapProfile> activeProfile() const;

signals:
    /** A key action starts a touch at guest pixel coordinates. */
    void touchDown(int x, int y);
    /** A key action releases a touch at guest pixel coordinates. */
    void touchUp(int x, int y);
    /** A key action moves an active touch to guest pixel coordinates. */
    void touchMove(int x, int y);
    /** A passthrough binding forwards a Qt key press or release. */
    void keyPassthrough(Qt::Key key, bool pressed);

private:
    struct SwipeState {
        QPointer<QTimer> timer;
        QPoint start;
        QPoint end;
        QPoint current;
        int steps{0};
        int step{0};
    };

    QPoint scaleCoords(int x, int y) const;
    void executeTap(const KeyAction& action);
    void executeSwipe(Qt::Key key, const KeyAction& action);
    void advanceSwipe(int keyValue);
    void stopSwipe(int keyValue, bool releaseAtCurrent);
    void startRepeatTap(int keyValue, const KeyAction& action);
    void stopRepeatTap(int keyValue);

    std::optional<KeymapProfile> m_profile;
    QSize m_guestResolution;
    bool m_enabled{false};
    QSet<int> m_pressedKeys;
    QMap<int, KeyAction> m_activeActions;
    QMap<int, QPointer<QTimer>> m_repeatTimers;
    QMap<int, SwipeState> m_swipes;
};
