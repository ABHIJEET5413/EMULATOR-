#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "adb_bridge.h"

#include <memory>

namespace {

constexpr int kConnectIntervalMs = 2000;
constexpr int kMaxConnectAttempts = 30;
constexpr int kBootPollIntervalMs = 3000;
constexpr int kHealthPollIntervalMs = 5000;

} // namespace

AdbBridge::AdbBridge(QObject* parent)
    : QObject(parent)
{
    m_connectTimer.setInterval(kConnectIntervalMs);
    m_bootPollTimer.setInterval(kBootPollIntervalMs);
    m_healthTimer.setInterval(kHealthPollIntervalMs);

    connect(&m_connectTimer, &QTimer::timeout, this, [this]() {
        tryConnect(m_connectionGeneration);
    });
    connect(&m_bootPollTimer, &QTimer::timeout, this, [this]() {
        pollBootStatus(m_connectionGeneration);
    });
    connect(&m_healthTimer, &QTimer::timeout, this, [this]() {
        pollConnectionHealth(m_connectionGeneration);
    });
}

void AdbBridge::setAdbPath(const QString& path)
{
    m_adbPath = path.trimmed();
    m_adbPathExplicit = !m_adbPath.isEmpty();
}

QString AdbBridge::adbPath() const
{
    return resolveAdbPath();
}

void AdbBridge::connectToGuest(const QString& host, const int port)
{
    const QString normalizedHost = host.trimmed();
    if (normalizedHost.isEmpty() || port < 1 || port > 65535) {
        emit errorOccurred(QStringLiteral("ADB guest address is invalid. Set a host and a port from 1 to 65535."));
        return;
    }

    if (m_connected && m_host == normalizedHost && m_port == port) {
        return;
    }

    const bool wasConnected = m_connected;
    ++m_connectionGeneration;
    m_connectTimer.stop();
    m_bootPollTimer.stop();
    m_healthTimer.stop();
    m_connected = false;
    m_bootCompleted = false;
    m_connectCommandRunning = false;
    m_bootCommandRunning = false;
    m_healthCommandRunning = false;
    m_connectAttempts = 0;
    m_host = normalizedHost;
    m_port = port;
    m_serial = QStringLiteral("%1:%2").arg(m_host).arg(m_port);

    if (wasConnected) {
        emit disconnected();
    }
    startAdbServer(m_connectionGeneration);
}

void AdbBridge::disconnectFromGuest()
{
    ++m_connectionGeneration;
    m_connectTimer.stop();
    m_bootPollTimer.stop();
    m_healthTimer.stop();
    m_connectCommandRunning = false;
    m_bootCommandRunning = false;
    m_healthCommandRunning = false;
    m_bootCompleted = false;
    const bool wasConnected = m_connected;
    m_connected = false;
    m_serial.clear();
    m_host.clear();
    m_port = 0;
    m_connectAttempts = 0;
    if (wasConnected) {
        emit disconnected();
    }
}

void AdbBridge::waitForBoot(const int timeoutMs)
{
    if (!m_connected) {
        emit errorOccurred(QStringLiteral("Cannot wait for Android boot: ADB is not connected to the guest."));
        return;
    }
    if (timeoutMs <= 0) {
        emit errorOccurred(QStringLiteral("Android boot timeout must be greater than zero."));
        return;
    }
    if (m_bootCompleted) {
        return;
    }

    m_bootTimeoutMs = timeoutMs;
    m_bootElapsed.start();
    m_bootPollTimer.start();
    pollBootStatus(m_connectionGeneration);
}

void AdbBridge::installApk(const QString& apkPath)
{
    const QFileInfo apkInfo(apkPath);
    if (!apkInfo.exists() || !apkInfo.isFile()
        || apkInfo.suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) != 0) {
        emit apkInstallFinished(false,
                                QStringLiteral("Select an existing Android .apk file to install."));
        return;
    }
    if (!requireBootCompleted(QStringLiteral("install an APK"))) {
        emit apkInstallFinished(false, QStringLiteral("Android is not ready yet. Wait for boot to complete."));
        return;
    }

    const QString absoluteApkPath = apkInfo.absoluteFilePath();
    emit installProgress(QStringLiteral("installing"));
    runAdbAsync({QStringLiteral("install"), QStringLiteral("-r"), absoluteApkPath},
                300000,
                [this, absoluteApkPath](const AdbResult& result) {
                    const QString output = QString::fromUtf8(result.standardOutput).trimmed();
                    const bool success = result.exitCode == 0
                        && output.contains(QStringLiteral("Success"), Qt::CaseInsensitive);
                    if (success) {
                        emit installProgress(QStringLiteral("done"));
                        emit apkInstallFinished(true,
                                                QStringLiteral("Installed %1 successfully.")
                                                    .arg(QFileInfo(absoluteApkPath).fileName()));
                        return;
                    }

                    emit installProgress(QStringLiteral("failed"));
                    const QString detail = resultMessage(result);
                    emit apkInstallFinished(false,
                                            QStringLiteral("Failed to install %1: %2")
                                                .arg(QFileInfo(absoluteApkPath).fileName(), detail));
                });
}

void AdbBridge::pushFile(const QString& localPath, const QString& remoteDirectory)
{
    const QFileInfo localInfo(localPath);
    if (!localInfo.exists() || !localInfo.isFile()) {
        emit filePushFinished(false, QStringLiteral("Select an existing local file to copy."));
        return;
    }
    if (!requireBootCompleted(QStringLiteral("copy a file"))) {
        emit filePushFinished(false, QStringLiteral("Android is not ready yet. Wait for boot to complete."));
        return;
    }

    QString remoteDir = QDir::fromNativeSeparators(remoteDirectory.trimmed());
    if (remoteDir.isEmpty()) {
        emit filePushFinished(false, QStringLiteral("The Android destination folder is empty."));
        return;
    }
    if (!remoteDir.endsWith(QLatin1Char('/'))) {
        remoteDir.append(QLatin1Char('/'));
    }
    const QString remotePath = remoteDir + localInfo.fileName();
    const QString absoluteLocalPath = localInfo.absoluteFilePath();

    runAdbAsync({QStringLiteral("push"), absoluteLocalPath, remotePath},
                300000,
                [this, remotePath](const AdbResult& result) {
                    if (result.exitCode == 0) {
                        emit filePushFinished(true,
                                              QStringLiteral("Copied file to %1.").arg(remotePath));
                        mediaScan(remotePath);
                        return;
                    }
                    emit filePushFinished(false,
                                          QStringLiteral("Failed to copy file to Android: %1")
                                              .arg(resultMessage(result)));
                });
}

void AdbBridge::mediaScan(const QString& remotePath)
{
    if (!requireBootCompleted(QStringLiteral("scan a copied file"))) {
        return;
    }

    const QUrl fileUrl = QUrl::fromLocalFile(QDir::fromNativeSeparators(remotePath));
    runAdbAsync({QStringLiteral("shell"), QStringLiteral("am"), QStringLiteral("broadcast"),
                 QStringLiteral("-a"), QStringLiteral("android.intent.action.MEDIA_SCANNER_SCAN_FILE"),
                 QStringLiteral("-d"), fileUrl.toString(QUrl::FullyEncoded)},
                30000,
                [this, remotePath](const AdbResult& result) {
                    if (result.exitCode != 0) {
                        emit errorOccurred(QStringLiteral("The file was copied to %1, but Android could not scan it: %2")
                                               .arg(remotePath, resultMessage(result)));
                    }
                });
}

void AdbBridge::screenshot(const QString& savePath)
{
    if (savePath.trimmed().isEmpty()) {
        return;
    }
    if (!requireBootCompleted(QStringLiteral("capture a screenshot"))) {
        return;
    }

    const QString absolutePath = QFileInfo(savePath).absoluteFilePath();
    runAdbAsync({QStringLiteral("exec-out"), QStringLiteral("screencap"), QStringLiteral("-p")},
                30000,
                [this, absolutePath](const AdbResult& result) {
                    static const QByteArray pngSignature("\x89PNG\r\n\x1a\n", 8);
                    if (result.exitCode != 0 || !result.standardOutput.startsWith(pngSignature)) {
                        emit errorOccurred(QStringLiteral("Screenshot failed: %1")
                                               .arg(resultMessage(result)));
                        return;
                    }

                    QSaveFile file(absolutePath);
                    if (!file.open(QIODevice::WriteOnly)
                        || file.write(result.standardOutput) != result.standardOutput.size()
                        || !file.commit()) {
                        emit errorOccurred(QStringLiteral("Could not save screenshot to '%1': %2")
                                               .arg(QDir::toNativeSeparators(absolutePath),
                                                    file.errorString()));
                        return;
                    }
                    emit screenshotSaved(absolutePath);
                });
}

void AdbBridge::sendKeyEvent(const int keycode)
{
    if (keycode < 0) {
        emit errorOccurred(QStringLiteral("Android key event code cannot be negative."));
        return;
    }
    if (!requireBootCompleted(QStringLiteral("send a navigation key"))) {
        return;
    }

    runAdbAsync({QStringLiteral("shell"), QStringLiteral("input"), QStringLiteral("keyevent"),
                 QString::number(keycode)},
                30000,
                [this, keycode](const AdbResult& result) {
                    if (result.exitCode != 0) {
                        emit errorOccurred(QStringLiteral("Android key event %1 failed: %2")
                                               .arg(keycode)
                                               .arg(resultMessage(result)));
                    }
                });
}

void AdbBridge::shellCommand(const QString& command)
{
    if (command.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("ADB shell command cannot be empty."));
        return;
    }
    if (!requireBootCompleted(QStringLiteral("run an Android shell command"))) {
        return;
    }

    runAdbAsync({QStringLiteral("shell"), command},
                30000,
                [this, command](const AdbResult& result) {
                    if (result.exitCode != 0) {
                        emit errorOccurred(QStringLiteral("Android shell command failed ('%1'): %2")
                                               .arg(command, resultMessage(result)));
                    }
                });
}

bool AdbBridge::isConnected() const
{
    return m_connected;
}

bool AdbBridge::isBootCompleted() const
{
    return m_bootCompleted;
}

QString AdbBridge::getDeviceSerial() const
{
    return m_serial;
}

QString AdbBridge::resolveAdbPath() const
{
    const auto isAdbFile = [](const QString& path) {
        const QFileInfo info(path);
        return info.exists() && info.isFile();
    };

    if (m_adbPathExplicit) {
        return isAdbFile(m_adbPath) ? QFileInfo(m_adbPath).absoluteFilePath() : QString();
    }

    QStringList candidates;
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    candidates << QDir(applicationDirectory).filePath(QStringLiteral("vm/adb/adb.exe"))
               << QDir(applicationDirectory).filePath(QStringLiteral("adb/adb.exe"))
               << QDir(applicationDirectory).filePath(QStringLiteral("../../vm/adb/adb.exe"))
               << QDir(QDir::currentPath()).filePath(QStringLiteral("vm/adb/adb.exe"))
               << QDir(QDir::currentPath()).filePath(QStringLiteral("adb/adb.exe"));

    for (const QString& candidate : candidates) {
        if (isAdbFile(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("adb.exe"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }
    return QStandardPaths::findExecutable(QStringLiteral("adb"));
}

quint64 AdbBridge::runAdbAsync(const QStringList& arguments,
                               const int timeoutMs,
                               Completion completion,
                               const bool useGuestSerial)
{
    const quint64 id = ++m_nextCommandId;
    const QString executable = resolveAdbPath();
    if (executable.isEmpty()) {
        AdbResult result;
        result.standardError = m_adbPathExplicit && !m_adbPath.isEmpty()
            ? QStringLiteral("The configured adb.exe was not found at '%1'.")
                  .arg(QDir::toNativeSeparators(m_adbPath)).toUtf8()
            : QByteArrayLiteral("adb.exe was not found. Expected it under vm/adb or adb beside the app, or on PATH.");
        emit commandFinished(id, result.exitCode, {}, QString::fromUtf8(result.standardError));
        if (completion) {
            completion(result);
        }
        return id;
    }

    QStringList fullArguments;
    if (useGuestSerial && !m_serial.isEmpty()) {
        fullArguments << QStringLiteral("-s") << m_serial;
    }
    fullArguments.append(arguments);
    const bool binaryOutput = !arguments.isEmpty()
        && arguments.first() == QStringLiteral("exec-out");
    qInfo().noquote() << "ADB:" << executable << fullArguments.join(QLatin1Char(' '));

    auto* process = new QProcess(this);
    process->setProgram(executable);
    process->setArguments(fullArguments);
    auto* timeoutTimer = new QTimer(process);
    timeoutTimer->setSingleShot(true);
    auto completed = std::make_shared<bool>(false);
    const auto finish = [this, process, timeoutTimer, completed, completion, id, binaryOutput](const AdbResult& result) {
        if (*completed) {
            return;
        }
        *completed = true;
        timeoutTimer->stop();
        emit commandFinished(id,
                             result.exitCode,
                             binaryOutput ? QString() : QString::fromUtf8(result.standardOutput).trimmed(),
                             QString::fromUtf8(result.standardError).trimmed());
        if (completion) {
            completion(result);
        }
        process->deleteLater();
    };

    connect(timeoutTimer, &QTimer::timeout, this, [process, finish]() {
        process->kill();
        AdbResult result;
        result.standardError = QByteArrayLiteral("ADB command timed out.");
        result.timedOut = true;
        finish(result);
    });
    connect(process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [process, finish](const int exitCode, const QProcess::ExitStatus exitStatus) {
                AdbResult result;
                result.exitCode = exitStatus == QProcess::NormalExit ? exitCode : -1;
                result.standardOutput = process->readAllStandardOutput();
                result.standardError = process->readAllStandardError();
                if (exitStatus == QProcess::CrashExit && result.standardError.isEmpty()) {
                    result.standardError = QByteArrayLiteral("adb.exe terminated unexpectedly.");
                }
                finish(result);
            });
    connect(process, &QProcess::errorOccurred, this,
            [process, finish](const QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) {
                    return;
                }
                AdbResult result;
                result.standardError = process->errorString().toUtf8();
                finish(result);
            });

    timeoutTimer->start(qMax(1, timeoutMs));
    process->start();
    return id;
}

void AdbBridge::startAdbServer(const quint64 generation)
{
    runAdbAsync({QStringLiteral("start-server")}, 15000,
                [this, generation](const AdbResult& result) {
                    if (generation != m_connectionGeneration) {
                        return;
                    }
                    if (result.exitCode != 0) {
                        emit errorOccurred(QStringLiteral("Could not start the ADB server: %1")
                                               .arg(resultMessage(result)));
                        return;
                    }
                    tryConnect(generation);
                },
                false);
}

void AdbBridge::tryConnect(const quint64 generation)
{
    if (generation != m_connectionGeneration || m_connected || m_connectCommandRunning) {
        return;
    }
    if (m_connectAttempts >= kMaxConnectAttempts) {
        m_connectTimer.stop();
        emit errorOccurred(QStringLiteral("Cannot connect to Android at %1. The VM may still be booting; check that ADB over TCP is enabled and the QEMU port forward uses %2.")
                               .arg(m_serial)
                               .arg(m_port));
        return;
    }

    ++m_connectAttempts;
    m_connectCommandRunning = true;
    runAdbAsync({QStringLiteral("connect"), m_serial}, 5000,
                [this, generation](const AdbResult& result) {
                    if (generation != m_connectionGeneration) {
                        return;
                    }
                    m_connectCommandRunning = false;
                    const QString output = QString::fromUtf8(result.standardOutput).trimmed();
                    const QString lowerOutput = output.toLower();
                    const bool connectionSucceeded = result.exitCode == 0
                        && (lowerOutput.contains(QStringLiteral("connected to"))
                            || lowerOutput.contains(QStringLiteral("already connected to")));
                    if (connectionSucceeded) {
                        m_connectTimer.stop();
                        m_connected = true;
                        emit connected(m_serial);
                        waitForBoot();
                        return;
                    }

                    qDebug().noquote() << "ADB connect attempt" << m_connectAttempts
                                       << "failed:" << resultMessage(result);
                    if (m_connectAttempts >= kMaxConnectAttempts) {
                        tryConnect(generation);
                    } else {
                        m_connectTimer.start(kConnectIntervalMs);
                    }
                },
                false);
}

void AdbBridge::pollBootStatus(const quint64 generation)
{
    if (generation != m_connectionGeneration || !m_connected || m_bootCompleted
        || m_bootCommandRunning) {
        return;
    }
    if (m_bootElapsed.elapsed() >= m_bootTimeoutMs) {
        m_bootPollTimer.stop();
        emit errorOccurred(QStringLiteral("Android did not finish booting within %1 seconds. Check the Bliss guest startup and ADB settings.")
                               .arg(m_bootTimeoutMs / 1000));
        return;
    }

    m_bootCommandRunning = true;
    runAdbAsync({QStringLiteral("shell"), QStringLiteral("getprop"),
                 QStringLiteral("sys.boot_completed")},
                10000,
                [this, generation](const AdbResult& result) {
                    if (generation != m_connectionGeneration) {
                        return;
                    }
                    m_bootCommandRunning = false;
                    if (!m_connected || m_bootCompleted) {
                        return;
                    }
                    if (result.exitCode == 0
                        && QString::fromUtf8(result.standardOutput).trimmed() == QStringLiteral("1")) {
                        m_bootPollTimer.stop();
                        m_bootCompleted = true;
                        m_healthTimer.start();
                        emit bootCompleted();
                        return;
                    }

                    qDebug().noquote() << "Waiting for Android boot:" << resultMessage(result);
                    m_bootPollTimer.start(kBootPollIntervalMs);
                });
}

void AdbBridge::pollConnectionHealth(const quint64 generation)
{
    if (generation != m_connectionGeneration || !m_connected || m_healthCommandRunning) {
        return;
    }

    m_healthCommandRunning = true;
    runAdbAsync({QStringLiteral("get-state")}, 5000,
                [this, generation](const AdbResult& result) {
                    if (generation != m_connectionGeneration) {
                        return;
                    }
                    m_healthCommandRunning = false;
                    if (result.exitCode == 0
                        && QString::fromUtf8(result.standardOutput).trimmed() == QStringLiteral("device")) {
                        return;
                    }
                    handleDisconnected(generation);
                });
}

void AdbBridge::handleDisconnected(const quint64 generation)
{
    if (generation != m_connectionGeneration) {
        return;
    }
    m_healthTimer.stop();
    m_bootPollTimer.stop();
    m_connected = false;
    m_bootCompleted = false;
    m_bootCommandRunning = false;
    m_healthCommandRunning = false;
    m_connectAttempts = 0;
    emit disconnected();
    m_connectTimer.start(0);
}

bool AdbBridge::requireBootCompleted(const QString& operation)
{
    if (m_bootCompleted) {
        return true;
    }
    emit errorOccurred(QStringLiteral("Cannot %1: Android is not ready yet. Wait for ADB connection and boot to complete.")
                           .arg(operation));
    return false;
}

QString AdbBridge::resultMessage(const AdbResult& result) const
{
    if (!result.standardError.trimmed().isEmpty()) {
        return QString::fromUtf8(result.standardError).trimmed();
    }
    if (!result.standardOutput.trimmed().isEmpty()) {
        return QString::fromUtf8(result.standardOutput).trimmed();
    }
    if (result.timedOut) {
        return QStringLiteral("ADB command timed out.");
    }
    return QStringLiteral("ADB exited with code %1.").arg(result.exitCode);
}
