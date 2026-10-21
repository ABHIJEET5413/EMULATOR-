#pragma once

#include <QMap>
#include <QSize>
#include <QString>
#include <Qt>

#include <optional>

/**
 * @file keymap_profile.h
 * @brief Defines key actions and the JSON-backed keymap profile model.
 */

/** A single action assigned to a physical Qt key. */
struct KeyAction {
    /** Action performed while the key is pressed. */
    enum Type {
        Tap,
        Hold,
        Swipe,
        Passthrough,
        RepeatTap
    };

    Type type{Passthrough};
    int x{0};
    int y{0};
    int startX{0};
    int startY{0};
    int endX{0};
    int endY{0};
    int durationMs{100};
    int intervalMs{50};
    QString description;
};

/** Mouse aim/drag metadata stored with a keymap profile. */
struct MouseMap {
    bool enabled{false};
    QString mode{QStringLiteral("aim")};
    double sensitivity{1.0};
    int centerX{0};
    int centerY{0};
    QString description;
};

/** Parsed key bindings and coordinate metadata from a keymap JSON file. */
class KeymapProfile final {
public:
    QString name;
    QString description;
    int version{1};
    QSize resolution{1080, 1920};
    QMap<Qt::Key, KeyAction> bindings;
    MouseMap mouseMap;

    /** Load and validate a profile; return no value and explain failures. */
    static std::optional<KeymapProfile> loadFromFile(
        const QString& path,
        QString* errorMessage = nullptr);

    /** Save this profile using the documented JSON schema. */
    bool saveToFile(const QString& path, QString* errorMessage = nullptr) const;

    /** Return the action for a key, or no value when it is not bound. */
    std::optional<KeyAction> actionForKey(Qt::Key key) const;

private:
    static Qt::Key parseKeyName(const QString& name);
};
