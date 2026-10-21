#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>

#include "keymap_engine.h"

namespace {

constexpr int kSwipeFrameMs = 16;
constexpr int kMaxSwipeFrames = 1200;

} // namespace

KeymapEngine::KeymapEngine(QObject* parent)
    : QObject(parent)
{
}

bool KeymapEngine::loadProfile(const QString& path, QString* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const std::optional<KeymapProfile> profile = KeymapProfile::loadFromFile(path, errorMessage);
    if (!profile) {
        return false;
    }

    releaseAll();
    m_profile = *profile;
    return true;
}

void KeymapEngine::setEnabled(const bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    if (!enabled) {
        releaseAll();
    }
    m_enabled = enabled;
}

bool KeymapEngine::isEnabled() const
{
    return m_enabled;
}

bool KeymapEngine::handleKeyPress(const Qt::Key key)
{
    if (!m_enabled || !m_profile) {
        return false;
    }

    const int keyValue = static_cast<int>(key);
    if (m_pressedKeys.contains(keyValue)) {
        return true;
    }

    const std::optional<KeyAction> action = m_profile->actionForKey(key);
    if (!action) {
        return false;
    }

    m_pressedKeys.insert(keyValue);
    m_activeActions.insert(keyValue, *action);
    switch (action->type) {
    case KeyAction::Tap:
        executeTap(*action);
        break;
    case KeyAction::Hold: {
        const QPoint point = scaleCoords(action->x, action->y);
        emit touchDown(point.x(), point.y());
        break;
    }
    case KeyAction::Swipe:
        executeSwipe(key, *action);
        break;
    case KeyAction::Passthrough:
        emit keyPassthrough(key, true);
        break;
    case KeyAction::RepeatTap:
        executeTap(*action);
        startRepeatTap(keyValue, *action);
        break;
    }
    return true;
}

bool KeymapEngine::handleKeyRelease(const Qt::Key key)
{
    const int keyValue = static_cast<int>(key);
    if (!m_pressedKeys.remove(keyValue)) {
        return false;
    }

    const auto actionIt = m_activeActions.find(keyValue);
    if (actionIt == m_activeActions.end()) {
        return true;
    }

    const KeyAction action = actionIt.value();
    switch (action.type) {
    case KeyAction::Tap:
        break;
    case KeyAction::Hold: {
        const QPoint point = scaleCoords(action.x, action.y);
        emit touchUp(point.x(), point.y());
        break;
    }
    case KeyAction::Swipe:
        stopSwipe(keyValue, true);
        break;
    case KeyAction::Passthrough:
        emit keyPassthrough(key, false);
        break;
    case KeyAction::RepeatTap:
        stopRepeatTap(keyValue);
        break;
    }
    m_activeActions.erase(actionIt);
    return true;
}

void KeymapEngine::releaseAll()
{
    const QList<int> pressedKeys = m_pressedKeys.values();
    for (const int keyValue : pressedKeys) {
        handleKeyRelease(static_cast<Qt::Key>(keyValue));
    }
    const QList<int> repeatKeys = m_repeatTimers.keys();
    for (const int keyValue : repeatKeys) {
        stopRepeatTap(keyValue);
    }
    const QList<int> swipeKeys = m_swipes.keys();
    for (const int keyValue : swipeKeys) {
        stopSwipe(keyValue, true);
    }
}

QStringList KeymapEngine::listProfiles(const QString& directory)
{
    const QDir profileDirectory(directory);
    const QFileInfoList files = profileDirectory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
    QStringList paths;
    paths.reserve(files.size());
    for (const QFileInfo& file : files) {
        paths.append(file.absoluteFilePath());
    }
    return paths;
}

void KeymapEngine::setGuestResolution(const QSize resolution)
{
    if (resolution.width() <= 0 || resolution.height() <= 0) {
        if (!resolution.isEmpty()) {
            qWarning() << "Ignoring invalid guest resolution:" << resolution;
        }
        m_guestResolution = QSize();
        return;
    }
    m_guestResolution = resolution;
}

QSize KeymapEngine::guestResolution() const
{
    if (m_guestResolution.width() > 0 && m_guestResolution.height() > 0) {
        return m_guestResolution;
    }
    return m_profile ? m_profile->resolution : QSize();
}

std::optional<KeymapProfile> KeymapEngine::activeProfile() const
{
    return m_profile;
}

QPoint KeymapEngine::scaleCoords(const int x, const int y) const
{
    if (!m_profile || m_profile->resolution.width() <= 0
        || m_profile->resolution.height() <= 0) {
        return QPoint(qMax(0, x), qMax(0, y));
    }

    const QSize targetResolution = guestResolution().isEmpty()
        ? m_profile->resolution
        : guestResolution();
    const int boundedX = qBound(0, x, m_profile->resolution.width() - 1);
    const int boundedY = qBound(0, y, m_profile->resolution.height() - 1);
    const double scaleX = static_cast<double>(targetResolution.width())
        / static_cast<double>(m_profile->resolution.width());
    const double scaleY = static_cast<double>(targetResolution.height())
        / static_cast<double>(m_profile->resolution.height());
    const int scaledX = qBound(0,
                               qRound(static_cast<double>(boundedX) * scaleX),
                               targetResolution.width() - 1);
    const int scaledY = qBound(0,
                               qRound(static_cast<double>(boundedY) * scaleY),
                               targetResolution.height() - 1);
    return QPoint(scaledX, scaledY);
}

void KeymapEngine::executeTap(const KeyAction& action)
{
    const QPoint point = scaleCoords(action.x, action.y);
    emit touchDown(point.x(), point.y());
    emit touchUp(point.x(), point.y());
}

void KeymapEngine::executeSwipe(const Qt::Key key, const KeyAction& action)
{
    const int keyValue = static_cast<int>(key);
    stopSwipe(keyValue, true);

    const QPoint start = scaleCoords(action.startX, action.startY);
    const QPoint end = scaleCoords(action.endX, action.endY);
    const int steps = qBound(2,
                             1 + (action.durationMs - 1) / kSwipeFrameMs,
                             kMaxSwipeFrames);

    SwipeState state;
    state.start = start;
    state.end = end;
    state.current = start;
    state.steps = steps;
    state.timer = new QTimer(this);
    state.timer->setInterval(qMax(1, action.durationMs / steps));
    connect(state.timer, &QTimer::timeout, this, [this, keyValue]() {
        advanceSwipe(keyValue);
    });
    m_swipes.insert(keyValue, state);
    emit touchDown(start.x(), start.y());
    state.timer->start();
}

void KeymapEngine::advanceSwipe(const int keyValue)
{
    auto stateIt = m_swipes.find(keyValue);
    if (stateIt == m_swipes.end()) {
        return;
    }

    SwipeState& state = stateIt.value();
    ++state.step;
    if (state.step >= state.steps) {
        emit touchUp(state.end.x(), state.end.y());
        if (state.timer) {
            state.timer->stop();
            state.timer->deleteLater();
        }
        m_swipes.erase(stateIt);
        return;
    }

    const double progress = static_cast<double>(state.step) / state.steps;
    state.current.setX(state.start.x()
                       + qRound(static_cast<double>(state.end.x() - state.start.x()) * progress));
    state.current.setY(state.start.y()
                       + qRound(static_cast<double>(state.end.y() - state.start.y()) * progress));
    emit touchMove(state.current.x(), state.current.y());
}

void KeymapEngine::stopSwipe(const int keyValue, const bool releaseAtCurrent)
{
    auto stateIt = m_swipes.find(keyValue);
    if (stateIt == m_swipes.end()) {
        return;
    }

    SwipeState state = stateIt.value();
    if (state.timer) {
        state.timer->stop();
        state.timer->deleteLater();
    }
    m_swipes.erase(stateIt);
    if (releaseAtCurrent) {
        emit touchUp(state.current.x(), state.current.y());
    }
}

void KeymapEngine::startRepeatTap(const int keyValue, const KeyAction& action)
{
    stopRepeatTap(keyValue);
    auto* timer = new QTimer(this);
    timer->setInterval(qMax(1, action.intervalMs));
    connect(timer, &QTimer::timeout, this, [this, action]() {
        executeTap(action);
    });
    m_repeatTimers.insert(keyValue, timer);
    timer->start();
}

void KeymapEngine::stopRepeatTap(const int keyValue)
{
    const auto timerIt = m_repeatTimers.find(keyValue);
    if (timerIt == m_repeatTimers.end()) {
        return;
    }
    if (timerIt.value()) {
        timerIt.value()->stop();
        timerIt.value()->deleteLater();
    }
    m_repeatTimers.erase(timerIt);
}
