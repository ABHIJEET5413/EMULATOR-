#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QPoint>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "keymap_engine.h"
#include "keymap_profile.h"

namespace {

bool check(const QString& name, const bool passed)
{
    QTextStream stream(passed ? stdout : stderr);
    stream << (passed ? "[PASS] " : "[FAIL] ") << name << Qt::endl;
    return passed;
}

bool writeTextFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(data) == data.size();
}

void waitForTimers(const int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_keymap"));

    bool allPassed = true;
    const QString profileDirectory = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("keymaps"));
    const QString defaultPath = QDir(profileDirectory).filePath(QStringLiteral("default.json"));
    const QString gamingPath = QDir(profileDirectory).filePath(QStringLiteral("wasd-gaming.json"));
    QString errorMessage;

    const auto defaultProfile = KeymapProfile::loadFromFile(defaultPath, &errorMessage);
    allPassed &= check(QStringLiteral("default profile loads"), defaultProfile.has_value());
    allPassed &= check(QStringLiteral("default profile keeps documented defaults"),
                       defaultProfile
                           && defaultProfile->version == 1
                           && defaultProfile->resolution == QSize(1080, 1920)
                           && defaultProfile->actionForKey(Qt::Key_Escape)->type
                               == KeyAction::Passthrough);

    const auto gamingProfile = KeymapProfile::loadFromFile(gamingPath, &errorMessage);
    allPassed &= check(QStringLiteral("WASD gaming profile loads"), gamingProfile.has_value());
    allPassed &= check(QStringLiteral("key names resolve to Qt keys"),
                       gamingProfile
                           && gamingProfile->actionForKey(Qt::Key_W).has_value()
                           && gamingProfile->actionForKey(Qt::Key_Space).has_value());
    allPassed &= check(QStringLiteral("profile includes all five action types"),
                       gamingProfile
                           && gamingProfile->actionForKey(Qt::Key_W)->type == KeyAction::Swipe
                           && gamingProfile->actionForKey(Qt::Key_Space)->type == KeyAction::Tap
                           && gamingProfile->actionForKey(Qt::Key_F)->type == KeyAction::Hold
                           && gamingProfile->actionForKey(Qt::Key_E)->type == KeyAction::RepeatTap
                           && gamingProfile->actionForKey(Qt::Key_Escape)->type
                               == KeyAction::Passthrough);
    allPassed &= check(QStringLiteral("unmapped key returns no action"),
                       gamingProfile
                           && !gamingProfile->actionForKey(Qt::Key_Z).has_value());
    allPassed &= check(QStringLiteral("mouse map metadata is parsed"),
                       gamingProfile
                           && gamingProfile->mouseMap.enabled
                           && gamingProfile->mouseMap.mode == QStringLiteral("aim")
                           && qFuzzyCompare(gamingProfile->mouseMap.sensitivity, 1.0));

    QTemporaryDir temporaryDirectory;
    const QString invalidPath = temporaryDirectory.filePath(QStringLiteral("invalid.json"));
    allPassed &= check(QStringLiteral("invalid JSON fixture can be written"),
                       writeTextFile(invalidPath, QByteArrayLiteral("{")));
    allPassed &= check(QStringLiteral("invalid JSON is rejected with an error"),
                       !KeymapProfile::loadFromFile(invalidPath, &errorMessage).has_value()
                           && !errorMessage.isEmpty());

    const QString minimalPath = temporaryDirectory.filePath(QStringLiteral("minimal.json"));
    allPassed &= check(QStringLiteral("minimal profile fixture can be written"),
                       writeTextFile(minimalPath, QByteArrayLiteral("{\"bindings\":[]}")));
    const auto minimalProfile = KeymapProfile::loadFromFile(minimalPath, &errorMessage);
    allPassed &= check(QStringLiteral("missing profile fields use defaults"),
                       minimalProfile
                           && minimalProfile->name == QStringLiteral("minimal")
                           && minimalProfile->version == 1
                           && minimalProfile->resolution == QSize(1080, 1920));

    const QString savedPath = temporaryDirectory.filePath(QStringLiteral("saved.json"));
    allPassed &= check(QStringLiteral("profile saves in the documented schema"),
                       gamingProfile
                           && gamingProfile->saveToFile(savedPath, &errorMessage));
    const auto reloadedProfile = KeymapProfile::loadFromFile(savedPath, &errorMessage);
    allPassed &= check(QStringLiteral("saved profile reloads with its bindings"),
                       reloadedProfile
                           && reloadedProfile->actionForKey(Qt::Key_W)->type == KeyAction::Swipe
                           && reloadedProfile->actionForKey(Qt::Key_Space)->type == KeyAction::Tap);

    const QStringList availableProfiles = KeymapEngine::listProfiles(profileDirectory);
    KeymapProfile punctuationProfile;
    punctuationProfile.resolution = QSize(1080, 1920);
    const QList<Qt::Key> punctuationKeys{Qt::Key_Minus, Qt::Key_Equal, Qt::Key_Slash,
                                        Qt::Key_PageUp, Qt::Key_PageDown};
    for (const Qt::Key key : punctuationKeys) {
        KeyAction action;
        action.type = KeyAction::Passthrough;
        punctuationProfile.bindings.insert(key, action);
    }
    const QString punctuationPath = temporaryDirectory.filePath(QStringLiteral("punctuation.json"));
    const bool punctuationSaved = punctuationProfile.saveToFile(punctuationPath, &errorMessage);
    const auto punctuationReloaded = KeymapProfile::loadFromFile(punctuationPath, &errorMessage);
    allPassed &= check(QStringLiteral("punctuation and page keys survive profile save and reload"),
                       punctuationSaved && punctuationReloaded
                           && punctuationReloaded->bindings.size() == punctuationKeys.size());
    allPassed &= check(QStringLiteral("profile listing finds both bundled profiles"),
                       availableProfiles.size() == 2
                           && availableProfiles.contains(defaultPath)
                           && availableProfiles.contains(gamingPath));

    KeymapEngine engine;
    allPassed &= check(QStringLiteral("engine loads a profile"), engine.loadProfile(gamingPath,
                                                                                     &errorMessage));
    engine.setGuestResolution(QSize(720, 1280));
    engine.setEnabled(true);

    QVector<QPoint> touchDowns;
    QVector<QPoint> touchUps;
    QVector<QPoint> touchMoves;
    QVector<QPair<Qt::Key, bool>> passthroughEvents;
    QObject::connect(&engine, &KeymapEngine::touchDown, &application,
                     [&touchDowns](const int x, const int y) {
                         touchDowns.append(QPoint(x, y));
                     });
    QObject::connect(&engine, &KeymapEngine::touchUp, &application,
                     [&touchUps](const int x, const int y) {
                         touchUps.append(QPoint(x, y));
                     });
    QObject::connect(&engine, &KeymapEngine::touchMove, &application,
                     [&touchMoves](const int x, const int y) {
                         touchMoves.append(QPoint(x, y));
                     });
    QObject::connect(&engine, &KeymapEngine::keyPassthrough, &application,
                     [&passthroughEvents](const Qt::Key key, const bool pressed) {
                         passthroughEvents.append(qMakePair(key, pressed));
                     });

    const int downCountBeforeTap = touchDowns.size();
    const int upCountBeforeTap = touchUps.size();
    const bool tapPressHandled = engine.handleKeyPress(Qt::Key_Space);
    const bool tapReleaseHandled = engine.handleKeyRelease(Qt::Key_Space);
    allPassed &= check(QStringLiteral("tap sends a scaled down/up pair"),
                       tapPressHandled && tapReleaseHandled
                           && touchDowns.size() == downCountBeforeTap + 1
                           && touchUps.size() == upCountBeforeTap + 1
                           && touchDowns.last() == QPoint(600, 1067)
                           && touchUps.last() == QPoint(600, 1067));

    const int downCountBeforeHold = touchDowns.size();
    const int upCountBeforeHold = touchUps.size();
    const bool holdPressHandled = engine.handleKeyPress(Qt::Key_F);
    const bool holdPressedOnly = touchDowns.size() == downCountBeforeHold + 1
        && touchUps.size() == upCountBeforeHold;
    const bool holdReleaseHandled = engine.handleKeyRelease(Qt::Key_F);
    allPassed &= check(QStringLiteral("hold stays down until key release"),
                       holdPressHandled && holdPressedOnly && holdReleaseHandled
                           && touchUps.size() == upCountBeforeHold + 1
                           && touchDowns.last() == QPoint(533, 800)
                           && touchUps.last() == QPoint(533, 800));

    const int moveCountBeforeSwipe = touchMoves.size();
    const int upCountBeforeSwipe = touchUps.size();
    const bool swipePressHandled = engine.handleKeyPress(Qt::Key_W);
    waitForTimers(180);
    const bool swipeReleaseHandled = engine.handleKeyRelease(Qt::Key_W);
    allPassed &= check(QStringLiteral("swipe emits intermediate moves and a touch release"),
                       swipePressHandled && swipeReleaseHandled
                           && touchMoves.size() > moveCountBeforeSwipe
                           && touchUps.size() == upCountBeforeSwipe + 1
                           && touchUps.last() == QPoint(360, 840));

    const int passthroughCountBefore = passthroughEvents.size();
    const bool passthroughPressHandled = engine.handleKeyPress(Qt::Key_Escape);
    const bool passthroughReleaseHandled = engine.handleKeyRelease(Qt::Key_Escape);
    allPassed &= check(QStringLiteral("passthrough forwards key down and key up"),
                       passthroughPressHandled && passthroughReleaseHandled
                           && passthroughEvents.size() == passthroughCountBefore + 2
                           && passthroughEvents.at(passthroughCountBefore)
                               == qMakePair(Qt::Key_Escape, true)
                           && passthroughEvents.at(passthroughCountBefore + 1)
                               == qMakePair(Qt::Key_Escape, false));

    const int downCountBeforeRepeat = touchDowns.size();
    const int upCountBeforeRepeat = touchUps.size();
    const bool repeatPressHandled = engine.handleKeyPress(Qt::Key_E);
    waitForTimers(260);
    const bool repeatReleaseHandled = engine.handleKeyRelease(Qt::Key_E);
    const int downCountAfterRelease = touchDowns.size();
    waitForTimers(140);
    allPassed &= check(QStringLiteral("repeat_tap repeats while held and stops on release"),
                       repeatPressHandled && repeatReleaseHandled
                           && touchDowns.size() >= downCountBeforeRepeat + 2
                           && touchUps.size() >= upCountBeforeRepeat + 2
                           && touchDowns.size() == downCountAfterRelease);

    const int downCountBeforeDisable = touchDowns.size();
    engine.setEnabled(false);
    allPassed &= check(QStringLiteral("disabled engine leaves keys unconsumed"),
                       !engine.handleKeyPress(Qt::Key_Space)
                           && touchDowns.size() == downCountBeforeDisable);

    return allPassed ? 0 : 1;
}
