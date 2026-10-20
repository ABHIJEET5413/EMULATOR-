# Phase 3: Keymap Engine — Detailed Implementation Guide

## Goal

Build a keymap engine that maps physical keyboard keys to screen taps,
swipes, holds, and key passthrough. Support loadable JSON profiles.

---

## Prerequisites

- [ ] Phase 2 complete and tested (display + input working)
- [ ] Input handler can send touch events to the guest

---

## Step-by-Step Implementation

### Step 1: Data Structures

```cpp
// keymap_profile.h

/// Represents a single key-to-action binding
struct KeyAction {
    /// Type of action to perform when the key is pressed
    enum Type {
        Tap,         ///< Single touch at (x, y)
        Hold,        ///< Touch-and-hold at (x, y) while key held
        Swipe,       ///< Swipe from (startX,startY) to (endX,endY)
        Passthrough, ///< Forward key to Android as keyboard input
        RepeatTap    ///< Tap repeatedly while key held
    };

    Type type = Passthrough;

    // For Tap, Hold, RepeatTap
    int x = 0;
    int y = 0;

    // For Swipe
    int startX = 0, startY = 0;
    int endX = 0, endY = 0;
    int durationMs = 100;

    // For RepeatTap
    int intervalMs = 50;

    // Human-readable description
    QString description;
};

/// Mouse mapping configuration for aim/camera controls
struct MouseMap {
    bool enabled = false;
    QString mode = "aim";   // "aim" or "drag"
    double sensitivity = 1.0;
    int centerX = 0;        // Center point for aim mode
    int centerY = 0;
};

/// A complete keymap profile loaded from JSON
class KeymapProfile {
public:
    QString name;
    QString description;
    int version = 1;
    QSize resolution;  // The resolution these coordinates target

    /// Load a profile from a JSON file
    static std::optional<KeymapProfile> loadFromFile(const QString& path);

    /// Save profile to JSON file
    bool saveToFile(const QString& path) const;

    /// Get the action for a Qt key, or nullopt if not mapped
    std::optional<KeyAction> actionForKey(Qt::Key key) const;

    /// All bindings
    QMap<Qt::Key, KeyAction> bindings;

    /// Mouse mapping
    MouseMap mouseMap;

private:
    /// Parse a Qt key name string to Qt::Key enum
    static Qt::Key parseKeyName(const QString& name);
};
```

### Step 2: JSON Parsing

```cpp
// keymap_profile.cpp

std::optional<KeymapProfile> KeymapProfile::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open keymap file:" << path;
        return std::nullopt;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error in keymap" << path << ":" << error.errorString();
        return std::nullopt;
    }

    QJsonObject root = doc.object();
    KeymapProfile profile;
    profile.name = root["name"].toString();
    profile.description = root["description"].toString();
    profile.version = root["version"].toInt(1);

    QJsonObject res = root["resolution"].toObject();
    profile.resolution = QSize(res["width"].toInt(), res["height"].toInt());

    // Parse bindings
    QJsonArray bindings = root["bindings"].toArray();
    for (const QJsonValue& val : bindings) {
        QJsonObject binding = val.toObject();
        QString keyName = binding["key"].toString();
        Qt::Key key = parseKeyName(keyName);
        if (key == Qt::Key_unknown) {
            qWarning() << "Unknown key name:" << keyName;
            continue;
        }

        KeyAction action;
        QString actionType = binding["action"].toString();
        QJsonObject params = binding["params"].toObject();

        if (actionType == "tap") {
            action.type = KeyAction::Tap;
            action.x = params["x"].toInt();
            action.y = params["y"].toInt();
        } else if (actionType == "hold") {
            action.type = KeyAction::Hold;
            action.x = params["x"].toInt();
            action.y = params["y"].toInt();
        } else if (actionType == "swipe") {
            action.type = KeyAction::Swipe;
            action.startX = params["startX"].toInt();
            action.startY = params["startY"].toInt();
            action.endX = params["endX"].toInt();
            action.endY = params["endY"].toInt();
            action.durationMs = params["durationMs"].toInt(100);
        } else if (actionType == "passthrough") {
            action.type = KeyAction::Passthrough;
        } else if (actionType == "repeat_tap") {
            action.type = KeyAction::RepeatTap;
            action.x = params["x"].toInt();
            action.y = params["y"].toInt();
            action.intervalMs = params["intervalMs"].toInt(50);
        }

        action.description = binding["description"].toString();
        profile.bindings[key] = action;
    }

    // Parse mouse map
    QJsonObject mm = root["mousemap"].toObject();
    profile.mouseMap.enabled = mm["enabled"].toBool();
    profile.mouseMap.mode = mm["mode"].toString("aim");
    profile.mouseMap.sensitivity = mm["sensitivity"].toDouble(1.0);
    profile.mouseMap.centerX = mm["centerX"].toInt();
    profile.mouseMap.centerY = mm["centerY"].toInt();

    return profile;
}
```

### Step 3: KeymapEngine Class

```cpp
// keymap_engine.h

class KeymapEngine : public QObject {
    Q_OBJECT

public:
    explicit KeymapEngine(QObject* parent = nullptr);

    /// Load a keymap profile from JSON file
    bool loadProfile(const QString& path);

    /// Enable/disable the keymap engine
    void setEnabled(bool enabled);
    bool isEnabled() const;

    /// Handle a key press. Returns true if the key was consumed.
    bool handleKeyPress(Qt::Key key);

    /// Handle a key release. Returns true if the key was consumed.
    bool handleKeyRelease(Qt::Key key);

    /// List available profile files in a directory
    static QStringList listProfiles(const QString& directory);

    /// Set the actual guest resolution (for coordinate scaling)
    void setGuestResolution(QSize resolution);

signals:
    /// Emitted when the engine wants to send a touch event
    void touchDown(int x, int y);
    void touchUp(int x, int y);
    void touchMove(int x, int y);
    void swipe(int startX, int startY, int endX, int endY, int durationMs);
    void keyPassthrough(Qt::Key key, bool pressed);

private:
    /// Scale coordinates from profile resolution to actual resolution
    QPoint scaleCoords(int x, int y) const;

    /// Execute a tap action
    void executeTap(const KeyAction& action);

    /// Execute a hold (touch down, release on key up)
    void executeHoldDown(const KeyAction& action);
    void executeHoldUp(const KeyAction& action);

    /// Execute a swipe
    void executeSwipe(const KeyAction& action);

    /// Start repeat-tap timer
    void startRepeatTap(Qt::Key key, const KeyAction& action);
    void stopRepeatTap(Qt::Key key);

    std::optional<KeymapProfile> m_profile;
    bool m_enabled = false;
    QSize m_guestResolution;

    /// Track which keys are currently held (for hold/repeat actions)
    QSet<Qt::Key> m_heldKeys;

    /// Timers for repeat-tap actions
    QMap<Qt::Key, QTimer*> m_repeatTimers;
};
```

### Step 4: Coordinate Scaling

```cpp
QPoint KeymapEngine::scaleCoords(int x, int y) const {
    if (!m_profile || m_guestResolution.isEmpty()) {
        return QPoint(x, y);
    }

    double scaleX = static_cast<double>(m_guestResolution.width()) /
                    m_profile->resolution.width();
    double scaleY = static_cast<double>(m_guestResolution.height()) /
                    m_profile->resolution.height();

    return QPoint(
        static_cast<int>(x * scaleX),
        static_cast<int>(y * scaleY)
    );
}
```

### Step 5: Integration with InputHandler

Modify `InputHandler` to check the keymap engine before forwarding keys:

```cpp
bool InputHandler::handleKeyEvent(QKeyEvent* event) {
    Qt::Key key = static_cast<Qt::Key>(event->key());

    // Check keymap engine first
    if (m_keymapEngine && m_keymapEngine->isEnabled()) {
        if (event->type() == QEvent::KeyPress) {
            if (m_keymapEngine->handleKeyPress(key)) {
                return true; // consumed by keymap
            }
        } else if (event->type() == QEvent::KeyRelease) {
            if (m_keymapEngine->handleKeyRelease(key)) {
                return true; // consumed by keymap
            }
        }
    }

    // Not consumed — forward as keyboard input to guest
    sendKeyToGuest(key, event->type() == QEvent::KeyPress);
    return true;
}
```

### Step 6: Swipe Implementation

Swipes require sending multiple touch events over time:

```cpp
void KeymapEngine::executeSwipe(const KeyAction& action) {
    QPoint start = scaleCoords(action.startX, action.startY);
    QPoint end = scaleCoords(action.endX, action.endY);
    int steps = action.durationMs / 16; // ~60 fps
    if (steps < 2) steps = 2;

    // Touch down at start
    emit touchDown(start.x(), start.y());

    // Animate the swipe with a timer
    auto timer = new QTimer(this);
    int step = 0;
    connect(timer, &QTimer::timeout, this, [=]() mutable {
        step++;
        if (step >= steps) {
            emit touchUp(end.x(), end.y());
            timer->stop();
            timer->deleteLater();
        } else {
            double t = static_cast<double>(step) / steps;
            int cx = start.x() + static_cast<int>((end.x() - start.x()) * t);
            int cy = start.y() + static_cast<int>((end.y() - start.y()) * t);
            emit touchMove(cx, cy);
        }
    });
    timer->start(16); // ~60 fps
}
```

### Step 7: Default Keymap Profiles

Create two example profiles:

**`host-app/resources/keymaps/default.json`** — Minimal, just passthrough
for common keys.

**`host-app/resources/keymaps/wasd-gaming.json`** — WASD movement,
mouse aim, common game controls.

---

## Unit Tests

`tests/test_keymap.cpp` should test:

1. **Parse valid JSON** → profile loads correctly
2. **Parse invalid JSON** → returns nullopt, no crash
3. **Missing fields** → uses defaults
4. **Key lookup** → mapped key returns correct action
5. **Unmapped key** → returns nullopt
6. **Coordinate scaling** → 1080→720 scales correctly
7. **Key name parsing** → "W" → Qt::Key_W, "Space" → Qt::Key_Space

```cpp
void testParseProfile() {
    auto profile = KeymapProfile::loadFromFile("test_profile.json");
    assert(profile.has_value());
    assert(profile->name == "Test");
    assert(profile->bindings.size() == 3);

    auto action = profile->actionForKey(Qt::Key_W);
    assert(action.has_value());
    assert(action->type == KeyAction::Swipe);
}
```
