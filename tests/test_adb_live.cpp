#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "adb_bridge.h"

#include <functional>

namespace {

bool waitUntil(const std::function<bool()>& condition, const int timeoutMs)
{
    if (condition()) {
        return true;
    }
    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (condition()) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(20);
    timeout.start(timeoutMs);
    loop.exec();
    return condition();
}

bool check(const QString& name, const bool success)
{
    QTextStream(success ? stdout : stderr) << (success ? "[PASS] " : "[FAIL] ")
                                         << name << Qt::endl;
    return success;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Live Phase 4 checks; requires a booted, authorized guest."));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("adb"), QStringLiteral("Path to adb.exe."), QStringLiteral("path"), QStringLiteral("vm/adb/adb.exe")});
    parser.addOption({QStringLiteral("apk"), QStringLiteral("APK to install or reinstall in the guest."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("Target instance's ADB host port."), QStringLiteral("port"), QStringLiteral("5555")});
    parser.process(application);
    if (!parser.isSet(QStringLiteral("apk"))) {
        QTextStream(stderr) << "Provide --apk with an APK you want to install in the test guest.\n";
        return 2;
    }
    bool portValid = false;
    const int port = parser.value(QStringLiteral("port")).toInt(&portValid);
    if (!portValid || port < 1024 || port > 65535) {
        QTextStream(stderr) << "Provide --port between 1024 and 65535.\n"; return 2;
    }

    AdbBridge bridge;
    bridge.setAdbPath(parser.value(QStringLiteral("adb")));
    QStringList errors;
    int completedCommands = 0;
    int failedCommands = 0;
    QObject::connect(&bridge, &AdbBridge::errorOccurred, &application,
                     [&](const QString& error) { errors.append(error); });
    QObject::connect(&bridge, &AdbBridge::commandFinished, &application,
                     [&](quint64, int code, const QString&, const QString&) {
                         ++completedCommands;
                         failedCommands += code != 0;
                     });
    bridge.connectToGuest(QStringLiteral("localhost"), port);
    if (!check(QStringLiteral("live bridge connects and verifies Android boot"),
               waitUntil([&]() { return bridge.isBootCompleted() || !errors.isEmpty(); }, 20000)
                   && bridge.isBootCompleted())) {
        QTextStream(stderr) << errors.join('\n') << Qt::endl;
        return 1;
    }

    bool installed = false;
    bool installSuccess = false;
    QObject::connect(&bridge, &AdbBridge::apkInstallFinished, &application,
                     [&](bool success, const QString& message) {
                         installed = true;
                         installSuccess = success;
                         QTextStream(stdout) << message << Qt::endl;
                     });
    bridge.installApk(parser.value(QStringLiteral("apk")));
    bool passed = check(QStringLiteral("real APK install completes"),
                        waitUntil([&]() { return installed; }, 300000) && installSuccess);

    QTemporaryDir directory;
    const QString pushedFile = directory.filePath(QStringLiteral("droidbox phase4 live.png"));
    QImage testImage(32, 32, QImage::Format_RGB32);
    testImage.fill(Qt::cyan);
    if (!check(QStringLiteral("live image fixture is saved"), directory.isValid() && testImage.save(pushedFile))) {
        return 1;
    }
    bool pushed = false;
    bool pushSuccess = false;
    QObject::connect(&bridge, &AdbBridge::filePushFinished, &application,
                     [&](bool success, const QString&) { pushed = true; pushSuccess = success; });
    const int beforePush = completedCommands;
    bridge.pushFile(pushedFile, QStringLiteral("/sdcard/Download/"));
    passed &= check(QStringLiteral("real file push and media scan complete"),
                    waitUntil([&]() { return pushed && completedCommands >= beforePush + 2; }, 30000)
                        && pushSuccess);

    const QString screenPath = directory.filePath(QStringLiteral("screenshot.png"));
    bool screenshotSaved = false;
    QObject::connect(&bridge, &AdbBridge::screenshotSaved, &application,
                     [&](const QString&) { screenshotSaved = true; });
    bridge.screenshot(screenPath);
    passed &= check(QStringLiteral("live guest screenshot decodes as a complete PNG"),
                    waitUntil([&]() { return screenshotSaved || !errors.isEmpty(); }, 30000)
                        && screenshotSaved && !QImage(screenPath).isNull());

    // Power is intentionally excluded to keep the test guest awake.
    const QList<int> keys{3, 4, 187, 24, 25, 3};
    for (int code : keys) {
        const int before = completedCommands;
        bridge.sendKeyEvent(code);
        passed &= check(QStringLiteral("live navigation key %1 completes").arg(code),
                        waitUntil([&]() { return completedCommands > before; }, 10000));
    }
    bridge.shellCommand(QStringLiteral("rm -f '/sdcard/Download/droidbox phase4 live.png'"));
    const int beforeCleanup = completedCommands;
    waitUntil([&]() { return completedCommands > beforeCleanup; }, 10000);
    passed &= check(QStringLiteral("all live commands return success"), failedCommands == 0 && errors.isEmpty());
    for (const QString& error : errors) {
        QTextStream(stderr) << error << Qt::endl;
    }
    bridge.disconnectFromGuest();
    return passed ? 0 : 1;
}
