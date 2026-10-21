#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

#include "keymap_profile.h"

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

bool readInteger(const QJsonObject& object,
                 const QString& name,
                 const int fallback,
                 int* result,
                 QString* errorMessage,
                 const bool required = false)
{
    const QJsonValue value = object.value(name);
    if (value.isUndefined()) {
        if (required) {
            setError(errorMessage, QStringLiteral("Missing integer field '%1'.").arg(name));
            return false;
        }
        *result = fallback;
        return true;
    }
    if (!value.isDouble()) {
        setError(errorMessage, QStringLiteral("Field '%1' must be an integer.").arg(name));
        return false;
    }

    const double number = value.toDouble();
    if (number < 0 || number > 2147483647.0 || number != static_cast<int>(number)) {
        setError(errorMessage,
                 QStringLiteral("Field '%1' must be a non-negative 32-bit integer.").arg(name));
        return false;
    }
    *result = static_cast<int>(number);
    return true;
}

QJsonObject actionToJson(const KeyAction& action)
{
    QString actionName;
    QJsonObject params;
    switch (action.type) {
    case KeyAction::Tap:
        actionName = QStringLiteral("tap");
        params = {{QStringLiteral("x"), action.x}, {QStringLiteral("y"), action.y}};
        break;
    case KeyAction::Hold:
        actionName = QStringLiteral("hold");
        params = {{QStringLiteral("x"), action.x}, {QStringLiteral("y"), action.y}};
        break;
    case KeyAction::Swipe:
        actionName = QStringLiteral("swipe");
        params = {{QStringLiteral("startX"), action.startX},
                  {QStringLiteral("startY"), action.startY},
                  {QStringLiteral("endX"), action.endX},
                  {QStringLiteral("endY"), action.endY},
                  {QStringLiteral("durationMs"), action.durationMs}};
        break;
    case KeyAction::Passthrough:
        actionName = QStringLiteral("passthrough");
        break;
    case KeyAction::RepeatTap:
        actionName = QStringLiteral("repeat_tap");
        params = {{QStringLiteral("x"), action.x},
                  {QStringLiteral("y"), action.y},
                  {QStringLiteral("intervalMs"), action.intervalMs}};
        break;
    }

    QJsonObject binding{{QStringLiteral("action"), actionName}};
    if (!params.isEmpty()) {
        binding.insert(QStringLiteral("params"), params);
    }
    if (!action.description.isEmpty()) {
        binding.insert(QStringLiteral("description"), action.description);
    }
    return binding;
}

} // namespace

std::optional<KeymapProfile> KeymapProfile::loadFromFile(
    const QString& path,
    QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage,
                 QStringLiteral("Cannot open keymap profile '%1': %2")
                     .arg(path, file.errorString()));
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(errorMessage,
                 QStringLiteral("Invalid JSON in keymap profile '%1': %2")
                     .arg(path, parseError.errorString()));
        return std::nullopt;
    }
    if (!document.isObject()) {
        setError(errorMessage,
                 QStringLiteral("Keymap profile '%1' must contain a JSON object.").arg(path));
        return std::nullopt;
    }

    const QJsonObject root = document.object();
    KeymapProfile profile;
    profile.name = root.value(QStringLiteral("name")).toString(
        QFileInfo(path).completeBaseName());
    profile.description = root.value(QStringLiteral("description")).toString();
    profile.version = root.value(QStringLiteral("version")).toInt(1);
    if (profile.version < 1) {
        setError(errorMessage, QStringLiteral("Keymap profile version must be at least 1."));
        return std::nullopt;
    }

    const QJsonValue resolutionValue = root.value(QStringLiteral("resolution"));
    if (!resolutionValue.isUndefined()) {
        if (!resolutionValue.isObject()) {
            setError(errorMessage, QStringLiteral("Profile 'resolution' must be an object."));
            return std::nullopt;
        }
        const QJsonObject resolution = resolutionValue.toObject();
        int width = 0;
        int height = 0;
        if (!readInteger(resolution, QStringLiteral("width"), 0, &width, errorMessage, true)
            || !readInteger(resolution, QStringLiteral("height"), 0, &height, errorMessage, true)
            || width <= 0 || height <= 0) {
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QStringLiteral("Profile resolution must have positive width and height.");
            }
            return std::nullopt;
        }
        profile.resolution = QSize(width, height);
    }

    const QJsonValue bindingsValue = root.value(QStringLiteral("bindings"));
    if (!bindingsValue.isUndefined() && !bindingsValue.isArray()) {
        setError(errorMessage, QStringLiteral("Profile 'bindings' must be an array."));
        return std::nullopt;
    }
    const QJsonArray bindings = bindingsValue.toArray();
    for (qsizetype index = 0; index < bindings.size(); ++index) {
        const QJsonValue bindingValue = bindings.at(index);
        if (!bindingValue.isObject()) {
            setError(errorMessage,
                     QStringLiteral("Binding %1 must be a JSON object.").arg(index));
            return std::nullopt;
        }

        const QJsonObject binding = bindingValue.toObject();
        const QString keyName = binding.value(QStringLiteral("key")).toString();
        const Qt::Key key = parseKeyName(keyName);
        if (key == Qt::Key_unknown) {
            qWarning().noquote() << QStringLiteral("Skipping unknown key name '%1' in %2.")
                                       .arg(keyName, path);
            continue;
        }
        if (profile.bindings.contains(key)) {
            setError(errorMessage,
                     QStringLiteral("Profile contains duplicate binding for key '%1'.")
                         .arg(keyName));
            return std::nullopt;
        }

        const QString actionName = binding.value(QStringLiteral("action")).toString();
        const QJsonValue paramsValue = binding.value(QStringLiteral("params"));
        if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
            setError(errorMessage,
                     QStringLiteral("Parameters for key '%1' must be an object.").arg(keyName));
            return std::nullopt;
        }
        const QJsonObject params = paramsValue.toObject();
        KeyAction action;
        const auto readPoint = [&](const QString& xName,
                                   const QString& yName,
                                   int* x,
                                   int* y) {
            return readInteger(params, xName, 0, x, errorMessage, true)
                && readInteger(params, yName, 0, y, errorMessage, true);
        };

        if (actionName == QStringLiteral("tap")) {
            action.type = KeyAction::Tap;
            if (!readPoint(QStringLiteral("x"), QStringLiteral("y"),
                           &action.x, &action.y)) {
                return std::nullopt;
            }
        } else if (actionName == QStringLiteral("hold")) {
            action.type = KeyAction::Hold;
            if (!readPoint(QStringLiteral("x"), QStringLiteral("y"),
                           &action.x, &action.y)) {
                return std::nullopt;
            }
        } else if (actionName == QStringLiteral("swipe")) {
            action.type = KeyAction::Swipe;
            if (!readPoint(QStringLiteral("startX"), QStringLiteral("startY"),
                           &action.startX, &action.startY)
                || !readPoint(QStringLiteral("endX"), QStringLiteral("endY"),
                              &action.endX, &action.endY)
                || !readInteger(params, QStringLiteral("durationMs"), 100,
                                &action.durationMs, errorMessage)
                || action.durationMs <= 0) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("Swipe duration must be positive.");
                }
                return std::nullopt;
            }
        } else if (actionName == QStringLiteral("passthrough")) {
            action.type = KeyAction::Passthrough;
        } else if (actionName == QStringLiteral("repeat_tap")) {
            action.type = KeyAction::RepeatTap;
            if (!readPoint(QStringLiteral("x"), QStringLiteral("y"),
                           &action.x, &action.y)
                || !readInteger(params, QStringLiteral("intervalMs"), 50,
                                &action.intervalMs, errorMessage)
                || action.intervalMs <= 0) {
                if (errorMessage && errorMessage->isEmpty()) {
                    *errorMessage = QStringLiteral("Repeat tap interval must be positive.");
                }
                return std::nullopt;
            }
        } else {
            setError(errorMessage,
                     QStringLiteral("Unsupported action '%1' for key '%2'.")
                         .arg(actionName, keyName));
            return std::nullopt;
        }

        action.description = binding.value(QStringLiteral("description")).toString();
        profile.bindings.insert(key, action);
    }

    const QJsonValue mouseMapValue = root.value(QStringLiteral("mousemap"));
    if (!mouseMapValue.isUndefined()) {
        if (!mouseMapValue.isObject()) {
            setError(errorMessage, QStringLiteral("Profile 'mousemap' must be an object."));
            return std::nullopt;
        }
        const QJsonObject mouseMap = mouseMapValue.toObject();
        profile.mouseMap.enabled = mouseMap.value(QStringLiteral("enabled")).toBool(false);
        profile.mouseMap.mode = mouseMap.value(QStringLiteral("mode")).toString(
            QStringLiteral("aim"));
        if (profile.mouseMap.mode != QStringLiteral("aim")
            && profile.mouseMap.mode != QStringLiteral("drag")) {
            setError(errorMessage,
                     QStringLiteral("Mouse map mode must be 'aim' or 'drag'."));
            return std::nullopt;
        }
        profile.mouseMap.sensitivity = mouseMap.value(QStringLiteral("sensitivity")).toDouble(1.0);
        if (profile.mouseMap.sensitivity <= 0.0) {
            setError(errorMessage, QStringLiteral("Mouse map sensitivity must be positive."));
            return std::nullopt;
        }
        if (!readInteger(mouseMap, QStringLiteral("centerX"), 0,
                         &profile.mouseMap.centerX, errorMessage)
            || !readInteger(mouseMap, QStringLiteral("centerY"), 0,
                            &profile.mouseMap.centerY, errorMessage)) {
            return std::nullopt;
        }
        profile.mouseMap.description = mouseMap.value(QStringLiteral("description")).toString();
    }

    return profile;
}

bool KeymapProfile::saveToFile(const QString& path, QString* errorMessage) const
{
    if (resolution.width() <= 0 || resolution.height() <= 0) {
        setError(errorMessage, QStringLiteral("Profile resolution must have positive dimensions."));
        return false;
    }

    QJsonArray bindingsJson;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        QJsonObject binding = actionToJson(it.value());
        binding.insert(QStringLiteral("key"), QKeySequence(it.key()).toString(QKeySequence::PortableText));
        bindingsJson.append(binding);
    }

    QJsonObject mouseMapJson{{QStringLiteral("enabled"), mouseMap.enabled},
                             {QStringLiteral("mode"), mouseMap.mode},
                             {QStringLiteral("sensitivity"), mouseMap.sensitivity},
                             {QStringLiteral("centerX"), mouseMap.centerX},
                             {QStringLiteral("centerY"), mouseMap.centerY}};
    if (!mouseMap.description.isEmpty()) {
        mouseMapJson.insert(QStringLiteral("description"), mouseMap.description);
    }

    const QJsonObject root{{QStringLiteral("name"), name},
                           {QStringLiteral("description"), description},
                           {QStringLiteral("version"), version},
                           {QStringLiteral("resolution"),
                            QJsonObject{{QStringLiteral("width"), resolution.width()},
                                        {QStringLiteral("height"), resolution.height()}}},
                           {QStringLiteral("bindings"), bindingsJson},
                           {QStringLiteral("mousemap"), mouseMapJson}};

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage,
                 QStringLiteral("Cannot save keymap profile '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        setError(errorMessage,
                 QStringLiteral("Could not finish saving keymap profile '%1': %2")
                     .arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::optional<KeyAction> KeymapProfile::actionForKey(const Qt::Key key) const
{
    const auto it = bindings.constFind(key);
    if (it == bindings.cend()) {
        return std::nullopt;
    }
    return it.value();
}

Qt::Key KeymapProfile::parseKeyName(const QString& name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized.size() == 1) {
        const QChar character = normalized.at(0);
        if (character >= QLatin1Char('a') && character <= QLatin1Char('z')) {
            return static_cast<Qt::Key>(Qt::Key_A + character.unicode() - QLatin1Char('a').unicode());
        }
        if (character >= QLatin1Char('0') && character <= QLatin1Char('9')) {
            return static_cast<Qt::Key>(Qt::Key_0 + character.unicode() - QLatin1Char('0').unicode());
        }
    }

    static const QMap<QString, Qt::Key> namedKeys{
        {QStringLiteral("space"), Qt::Key_Space},
        {QStringLiteral("escape"), Qt::Key_Escape},
        {QStringLiteral("esc"), Qt::Key_Escape},
        {QStringLiteral("enter"), Qt::Key_Return},
        {QStringLiteral("return"), Qt::Key_Return},
        {QStringLiteral("tab"), Qt::Key_Tab},
        {QStringLiteral("backspace"), Qt::Key_Backspace},
        {QStringLiteral("delete"), Qt::Key_Delete},
        {QStringLiteral("insert"), Qt::Key_Insert},
        {QStringLiteral("home"), Qt::Key_Home},
        {QStringLiteral("end"), Qt::Key_End},
        {QStringLiteral("pageup"), Qt::Key_PageUp},
        {QStringLiteral("pagedown"), Qt::Key_PageDown},
        {QStringLiteral("up"), Qt::Key_Up},
        {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left},
        {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("shift"), Qt::Key_Shift},
        {QStringLiteral("control"), Qt::Key_Control},
        {QStringLiteral("ctrl"), Qt::Key_Control},
        {QStringLiteral("alt"), Qt::Key_Alt},
        {QStringLiteral("capslock"), Qt::Key_CapsLock},
        {QStringLiteral("minus"), Qt::Key_Minus},
        {QStringLiteral("equal"), Qt::Key_Equal},
        {QStringLiteral("bracketleft"), Qt::Key_BracketLeft},
        {QStringLiteral("bracketright"), Qt::Key_BracketRight},
        {QStringLiteral("semicolon"), Qt::Key_Semicolon},
        {QStringLiteral("apostrophe"), Qt::Key_Apostrophe},
        {QStringLiteral("quoteleft"), Qt::Key_QuoteLeft},
        {QStringLiteral("backslash"), Qt::Key_Backslash},
        {QStringLiteral("comma"), Qt::Key_Comma},
        {QStringLiteral("period"), Qt::Key_Period},
        {QStringLiteral("slash"), Qt::Key_Slash},
        {QStringLiteral("print"), Qt::Key_Print},
        {QStringLiteral("pause"), Qt::Key_Pause},
        {QStringLiteral("menu"), Qt::Key_Menu},
        {QStringLiteral("meta"), Qt::Key_Meta},
    };
    const auto named = namedKeys.constFind(normalized);
    if (named != namedKeys.cend()) {
        return named.value();
    }

    static const QRegularExpression functionKeyPattern(QStringLiteral("^f([1-9]|1[0-2])$"));
    const QRegularExpressionMatch functionKeyMatch = functionKeyPattern.match(normalized);
    if (functionKeyMatch.hasMatch()) {
        const int functionKeyNumber = functionKeyMatch.captured(1).toInt();
        return static_cast<Qt::Key>(Qt::Key_F1 + functionKeyNumber - 1);
    }
    // saveToFile uses Qt's portable spelling, including punctuation and PgUp/PgDown.
    const QKeySequence sequence = QKeySequence::fromString(name, QKeySequence::PortableText);
    if (sequence.count() == 1 && sequence[0].keyboardModifiers() == Qt::NoModifier) {
        const Qt::Key key = sequence[0].key();
        if (key != Qt::Key_unknown && key != static_cast<Qt::Key>(0)) {
            return key;
        }
    }
    return Qt::Key_unknown;
}
