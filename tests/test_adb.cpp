#include <QCoreApplication>
#include <QBuffer>
#include <QImage>
#include <QEventLoop>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>

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
    if (command == QStringLiteral("kill-server")) {
        return 0;
    } else if (command == QStringLiteral("start-server")) {
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
            std::fputs("error: device offline\n", stderr);
            return 1;
        } else {
            output = QByteArrayLiteral("device\n");
        }
    } else if (command == QStringLiteral("install")) {
        if (qEnvironmentVariableIsSet("DROIDBOX_FAKE_ADB_SLOW_INSTALL")) {
            QThread::msleep(400);
        }
        output = QByteArrayLiteral("Performing Streamed Install\nSuccess\n");
    } else if (command == QStringLiteral("push")) {
        output = QByteArrayLiteral("1 file pushed\n");
    } else if (command == QStringLiteral("exec-out")) {
        QByteArray pngBytes;
        if (qEnvironmentVariableIsSet("DROIDBOX_FAKE_ADB_BAD_PNG")) {
            pngBytes = QByteArray("\x89PNG\r\n\x1a\n", 8);
        } else {
            QImage pixel(1, 1, QImage::Format_RGB32);
            pixel.fill(Qt::black);
            QBuffer buffer(&pngBytes);
            buffer.open(QIODevice::WriteOnly);
            if (!pixel.save(&buffer, "PNG")) {
                return 1;
            }
        }
        std::fwrite(pngBytes.constData(), 1, static_cast<size_t>(pngBytes.size()), stdout);
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
        || first == QStringLiteral("kill-server")
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

    QTcpServer listener;
    allPassed &= check(QStringLiteral("mock guest listener starts on a private ephemeral port"),
                       listener.listen(QHostAddress::LocalHost, 0));
    const int port = listener.serverPort();
    const QString serial = QStringLiteral("localhost:%1").arg(port);
    bool guestServiceReady = false;
    QObject::connect(&listener, &QTcpServer::newConnection, &application, [&]() {
        while (QTcpSocket* socket = listener.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                const QByteArray request = socket->readAll();
                if (!guestServiceReady) {
                    socket->disconnectFromHost();
                } else if (request.startsWith("CNXN")) {
                    socket->write(QByteArray::fromHex("4155544801000000000000001400000000000000beaaabb7")
                                  + QByteArray(20, '\0'));
                }
            });
        }
    });

    AdbBridge bridge;
    bridge.setAdbPath(QCoreApplication::applicationFilePath());
    bool bootCompleted = false;
    QObject::connect(&bridge, &AdbBridge::bootCompleted, &application, [&bootCompleted]() {
        bootCompleted = true;
    });
    bridge.connectToGuest(QStringLiteral("localhost"), port);
    waitUntil([&]() { return logContains(logPath, QByteArrayLiteral("connect|")); }, 300);
    allPassed &= check(QStringLiteral("open forwarding socket cannot trigger ADB connect before guest protocol is ready"),
                       !logContains(logPath, QByteArrayLiteral("connect|")) && !bridge.isConnected());
    guestServiceReady = true;
    allPassed &= check(QStringLiteral("ADB connects and Android boot polling completes"),
                       waitUntil([&bootCompleted]() { return bootCompleted; }));
    allPassed &= check(QStringLiteral("connected serial matches the QEMU port forward"),
                       bridge.isConnected()
                           && bridge.isBootCompleted()
                           && bridge.getDeviceSerial() == serial);
    allPassed &= check(QStringLiteral("starting DroidBox preserves the shared ADB server"),
                       !logContains(logPath, QByteArrayLiteral("kill-server")));

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
    qputenv("DROIDBOX_FAKE_ADB_BAD_PNG", "1");
    bool badPngReported = false;
    QObject::connect(&bridge, &AdbBridge::errorOccurred, &application,
                     [&badPngReported](const QString& message) {
                         badPngReported |= message.contains(QStringLiteral("invalid PNG"));
                     });
    const QString invalidScreenshot = temporaryDirectory.filePath(QStringLiteral("invalid.png"));
    bridge.screenshot(invalidScreenshot);
    allPassed &= check(QStringLiteral("truncated PNG is rejected without saving a screenshot"),
                       waitUntil([&]() { return badPngReported; }) && !QFile::exists(invalidScreenshot));
    qunsetenv("DROIDBOX_FAKE_ADB_BAD_PNG");

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

    installFinished = false;
    qputenv("DROIDBOX_FAKE_ADB_SLOW_INSTALL", "1");
    bridge.installApk(apkPath);
    bridge.disconnectFromGuest();
    waitUntil([&installFinished]() { return installFinished; }, 700);
    allPassed &= check(QStringLiteral("late install completion cannot report success after disconnect"),
                       !installFinished);
    qunsetenv("DROIDBOX_FAKE_ADB_SLOW_INSTALL");
    allPassed &= check(QStringLiteral("disconnect clears guest readiness"),
                       !bridge.isConnected() && !bridge.isBootCompleted());

    QFile::remove(offlineStatePath);
    qputenv("DROIDBOX_FAKE_ADB_OFFLINE_ONCE_FILE", offlineStatePath.toLocal8Bit());
    AdbBridge recoveryBridge;
    recoveryBridge.setAdbPath(QCoreApplication::applicationFilePath());
    bool recoveredBootCompleted = false;
    bool offlineReported = false;
    QObject::connect(&recoveryBridge, &AdbBridge::errorOccurred, &application,
                     [&offlineReported](const QString& message) {
                         offlineReported = message.contains(QStringLiteral("Automatic reconnect has stopped"));
                     });
    QObject::connect(&recoveryBridge, &AdbBridge::bootCompleted, &application,
                     [&recoveredBootCompleted]() { recoveredBootCompleted = true; });
    recoveryBridge.connectToGuest(QStringLiteral("localhost"), port);
    allPassed &= check(QStringLiteral("offline transport stops retries and reports guest diagnostics"),
                       waitUntil([&offlineReported]() { return offlineReported; })
                           && !recoveryBridge.isConnected());
    // The fixture repairs its guest state after the first query. Reconnect explicitly.
    recoveryBridge.connectToGuest(QStringLiteral("localhost"), port);
    allPassed &= check(QStringLiteral("explicit reconnect succeeds after the guest is repaired"),
                       waitUntil([&recoveredBootCompleted]() { return recoveredBootCompleted; }, 10000)
                           && recoveryBridge.isConnected()
                           && recoveryBridge.isBootCompleted());
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
