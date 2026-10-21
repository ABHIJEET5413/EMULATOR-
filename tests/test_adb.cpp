#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include "adb_bridge.h"

#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace {

bool check(const QString& name, const bool passed)
{
    QTextStream stream(passed ? stdout : stderr);
    stream << (passed ? "[PASS] " : "[FAIL] ") << name << Qt::endl;
    return passed;
}

bool waitUntil(const std::function<bool()>& condition, const int timeoutMs = 8000)
{
    if (condition()) {
        return true;
    }

    QEventLoop loop;
    QTimer pollTimer;
    pollTimer.setInterval(10);
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&pollTimer, &QTimer::timeout, &loop, [&]() {
        if (condition()) {
            loop.quit();
        }
    });
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    pollTimer.start();
    timeoutTimer.start(timeoutMs);
    loop.exec();
    return condition();
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool logContains(const QString& logPath, const QByteArray& text)
{
    QFile file(logPath);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(text);
}

int runFakeAdb(const int argc, char* argv[])
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    QStringList arguments;
    for (int index = 1; index < argc; ++index) {
        arguments.append(QString::fromLocal8Bit(argv[index]));
    }

    int commandIndex = 0;
    if (arguments.size() >= 3 && arguments.at(0) == QStringLiteral("-s")) {
        commandIndex = 2;
    }
    const QString command = arguments.value(commandIndex);
    const QStringList commandArguments = arguments.mid(commandIndex + 1);

    const QByteArray logPath = qgetenv("DROIDBOX_FAKE_ADB_LOG");
    if (!logPath.isEmpty()) {
        QFile log(QString::fromLocal8Bit(logPath));
        if (log.open(QIODevice::WriteOnly | QIODevice::Append)) {
            log.write(arguments.join(QLatin1Char('|')).toUtf8());
            log.write("\n");
        }
    }

    QByteArray output;
    if (command == QStringLiteral("start-server")) {
        output = QByteArrayLiteral("daemon started successfully\n");
    } else if (command == QStringLiteral("connect")) {
        output = QByteArrayLiteral("connected to ")
            + commandArguments.value(0).toUtf8() + QByteArrayLiteral("\n");
    } else if (command == QStringLiteral("shell")
               && commandArguments.value(0) == QStringLiteral("getprop")) {
        output = QByteArrayLiteral("1\n");
    } else if (command == QStringLiteral("get-state")) {
        const QString stateFile = QString::fromLocal8Bit(qgetenv("DROIDBOX_FAKE_ADB_OFFLINE_ONCE_FILE"));
        if (!stateFile.isEmpty() && !QFile::exists(stateFile)) {
            writeFile(stateFile, QByteArrayLiteral("offline\n"));
            output = QByteArrayLiteral("offline\n");
        } else {
            output = QByteArrayLiteral("device\n");
        }
    } else if (command == QStringLiteral("install")) {
        output = QByteArrayLiteral("Performing Streamed Install\nSuccess\n");
    } else if (command == QStringLiteral("push")) {
        output = QByteArrayLiteral("1 file pushed\n");
    } else if (command == QStringLiteral("exec-out")) {
        static const char pngBytes[] = {
            static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
            0, 0, 0, 0
        };
        std::fwrite(pngBytes, 1, sizeof(pngBytes), stdout);
        std::fflush(stdout);
        return 0;
    } else if (command == QStringLiteral("shell")
               && commandArguments.value(0) == QStringLiteral("am")) {
        output = QByteArrayLiteral("Broadcast completed: result=0\n");
    }

    if (!output.isEmpty()) {
        std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
        std::fflush(stdout);
    }
    return 0;
}

bool isFakeAdbInvocation(const int argc, char* argv[])
{
    if (argc < 2) {
        return false;
    }
    const QString first = QString::fromLocal8Bit(argv[1]);
    if (first == QStringLiteral("-s")) {
        return argc >= 4;
    }
    return first == QStringLiteral("start-server")
        || first == QStringLiteral("connect")
        || first == QStringLiteral("disconnect");
}

} // namespace

int main(int argc, char* argv[])
{
    if (isFakeAdbInvocation(argc, argv)) {
        return runFakeAdb(argc, argv);
    }

    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_adb"));

    bool allPassed = true;
    QTemporaryDir temporaryDirectory;
    const QString logPath = temporaryDirectory.filePath(QStringLiteral("adb-commands.log"));
    const QString apkPath = temporaryDirectory.filePath(QStringLiteral("sample.apk"));
    const QString imagePath = temporaryDirectory.filePath(QStringLiteral("sample image.png"));
    const QString screenshotPath = temporaryDirectory.filePath(QStringLiteral("screenshot.png"));
    const QString offlineStatePath = temporaryDirectory.filePath(QStringLiteral("offline-once.state"));
    allPassed &= check(QStringLiteral("temporary directory is available"), temporaryDirectory.isValid());
    allPassed &= check(QStringLiteral("APK fixture is created"), writeFile(apkPath, QByteArrayLiteral("fake apk")));
    allPassed &= check(QStringLiteral("file-push fixture is created"), writeFile(imagePath, QByteArrayLiteral("image")));
    qputenv("DROIDBOX_FAKE_ADB_LOG", logPath.toLocal8Bit());

    AdbBridge bridge;
    bridge.setAdbPath(QCoreApplication::applicationFilePath());
    bool bootCompleted = false;
    QObject::connect(&bridge, &AdbBridge::bootCompleted, &application, [&bootCompleted]() {
        bootCompleted = true;
    });
    bridge.connectToGuest(QStringLiteral("localhost"), 5555);
    allPassed &= check(QStringLiteral("ADB connects and Android boot polling completes"),
                       waitUntil([&bootCompleted]() { return bootCompleted; }));
    allPassed &= check(QStringLiteral("connected serial matches the QEMU port forward"),
                       bridge.isConnected()
                           && bridge.isBootCompleted()
                           && bridge.getDeviceSerial() == QStringLiteral("localhost:5555"));

    bool installFinished = false;
    bool installSucceeded = false;
    QObject::connect(&bridge, &AdbBridge::apkInstallFinished, &application,
                     [&installFinished, &installSucceeded](const bool success, const QString&) {
                         installFinished = true;
                         installSucceeded = success;
                     });
    bridge.installApk(apkPath);
    allPassed &= check(QStringLiteral("APK install reports success"),
                       waitUntil([&installFinished]() { return installFinished; })
                           && installSucceeded);

    bool pushFinished = false;
    bool pushSucceeded = false;
    QObject::connect(&bridge, &AdbBridge::filePushFinished, &application,
                     [&pushFinished, &pushSucceeded](const bool success, const QString&) {
                         pushFinished = true;
                         pushSucceeded = success;
                     });
    bridge.pushFile(imagePath, QStringLiteral("/sdcard/Download/"));
    allPassed &= check(QStringLiteral("file push reports success"),
                       waitUntil([&pushFinished]() { return pushFinished; })
                           && pushSucceeded);
    allPassed &= check(QStringLiteral("file push requests an Android media scan"),
                       waitUntil([&logPath]() {
                           return logContains(logPath,
                                              QByteArrayLiteral("android.intent.action.MEDIA_SCANNER_SCAN_FILE"));
                       })
                           && logContains(logPath, QByteArrayLiteral("/sdcard/Download/sample image.png")));

    bool screenshotFinished = false;
    QObject::connect(&bridge, &AdbBridge::screenshotSaved, &application,
                     [&screenshotFinished](const QString&) { screenshotFinished = true; });
    bridge.screenshot(screenshotPath);
    const bool screenshotSignalReceived = waitUntil([&screenshotFinished]() {
        return screenshotFinished;
    });
    QFile screenshot(screenshotPath);
    const bool screenshotIsPng = screenshot.open(QIODevice::ReadOnly)
        && screenshot.read(8) == QByteArray("\x89PNG\r\n\x1a\n", 8);
    allPassed &= check(QStringLiteral("screenshot is saved as PNG"),
                       screenshotSignalReceived && screenshotIsPng);

    const QList<int> navigationKeycodes{3, 4, 187, 24, 25, 26};
    for (const int keycode : navigationKeycodes) {
        bridge.sendKeyEvent(keycode);
    }
    allPassed &= check(QStringLiteral("all navigation key events reach ADB"),
                       waitUntil([&logPath, &navigationKeycodes]() {
                           for (const int keycode : navigationKeycodes) {
                               if (!logContains(logPath,
                                                QByteArrayLiteral("input|keyevent|")
                                                    + QByteArray::number(keycode))) {
                                   return false;
                               }
                           }
                           return true;
                       }));

    bridge.disconnectFromGuest();
    allPassed &= check(QStringLiteral("disconnect clears guest readiness"),
                       !bridge.isConnected() && !bridge.isBootCompleted());

    QFile::remove(offlineStatePath);
    qputenv("DROIDBOX_FAKE_ADB_OFFLINE_ONCE_FILE", offlineStatePath.toLocal8Bit());
    AdbBridge recoveryBridge;
    recoveryBridge.setAdbPath(QCoreApplication::applicationFilePath());
    bool recoveredBootCompleted = false;
    QObject::connect(&recoveryBridge, &AdbBridge::bootCompleted, &application,
                     [&recoveredBootCompleted]() { recoveredBootCompleted = true; });
    recoveryBridge.connectToGuest(QStringLiteral("localhost"), 5555);
    allPassed &= check(QStringLiteral("offline ADB transport is disconnected and recovered"),
                       waitUntil([&recoveredBootCompleted]() { return recoveredBootCompleted; }, 10000)
                           && recoveryBridge.isConnected()
                           && recoveryBridge.isBootCompleted()
                           && logContains(logPath,
                                          QByteArrayLiteral("disconnect|localhost:5555")));
    recoveryBridge.disconnectFromGuest();
    qunsetenv("DROIDBOX_FAKE_ADB_OFFLINE_ONCE_FILE");

    AdbBridge missingAdbBridge;
    missingAdbBridge.setAdbPath(temporaryDirectory.filePath(QStringLiteral("missing-adb.exe")));
    bool missingAdbReported = false;
    QObject::connect(&missingAdbBridge, &AdbBridge::errorOccurred, &application,
                     [&missingAdbReported](const QString& message) {
                         missingAdbReported = message.contains(QStringLiteral("adb.exe"), Qt::CaseInsensitive);
                     });
    missingAdbBridge.connectToGuest(QStringLiteral("localhost"), 5555);
    allPassed &= check(QStringLiteral("missing ADB reports a readable error"), missingAdbReported);

    qunsetenv("DROIDBOX_FAKE_ADB_LOG");
    return allPassed ? 0 : 1;
}
