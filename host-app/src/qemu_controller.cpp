#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QRegularExpression>

#include "qemu_controller.h"

namespace {

QString formatPort(const int port)
{
    return QString::number(port);
}

bool validPort(const int port)
{
    return port > 0 && port <= 65535;
}

QString normalizedAbsolutePath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

QemuController::QemuController(QObject* parent)
    : QObject(parent)
{
    m_qmpTimeoutTimer.setSingleShot(true);
    m_shutdownTimer.setSingleShot(true);

    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &QemuController::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred,
            this, &QemuController::handleProcessError);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &QemuController::handleStandardError);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &QemuController::handleStandardOutput);
    connect(&m_snapshotProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &QemuController::handleSnapshotProcessFinished);
    connect(&m_snapshotProcess, &QProcess::errorOccurred,
            this, &QemuController::handleSnapshotProcessError);

    connect(&m_qmpClient, &QmpClient::connected,
            this, &QemuController::handleQmpConnected);
    connect(&m_qmpClient, &QmpClient::responseReceived,
            this, &QemuController::handleQmpResponse);
    connect(&m_qmpClient, &QmpClient::commandFailed,
            this, &QemuController::handleQmpCommandFailure);
    connect(&m_qmpClient, &QmpClient::eventReceived,
            this, &QemuController::handleQmpEvent);
    connect(&m_qmpClient, &QmpClient::errorOccurred,
            this, &QemuController::handleQmpError);
    connect(&m_qmpTimeoutTimer, &QTimer::timeout,
            this, &QemuController::handleQmpTimeout);
    connect(&m_shutdownTimer, &QTimer::timeout,
            this, &QemuController::handleShutdownTimeout);
}

QemuController::~QemuController()
{
    m_qmpTimeoutTimer.stop();
    m_shutdownTimer.stop();
    m_qmpClient.disconnectFromHost();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
    if (m_snapshotProcess.state() != QProcess::NotRunning) {
        m_snapshotProcess.kill();
        m_snapshotProcess.waitForFinished(3000);
    }
}

bool QemuController::start(const QString& configPath)
{
    if (m_snapshotProcess.state() != QProcess::NotRunning) {
        reportError(QStringLiteral("Wait for the disk snapshot operation to finish before starting QEMU."));
        return false;
    }
    if (isRunning()) {
        reportError(QStringLiteral("QEMU is already running."));
        return false;
    }

    m_qmpTimeoutTimer.stop();
    m_shutdownTimer.stop();
    m_pendingCommands.clear();
    m_standardError.clear();
    m_standardOutput.clear();
    m_vmStartedEmitted = false;
    m_forceKillRequested = false;
    m_qmpQuitRequested = false;
    m_errorReported = false;
    m_vmStatus = QStringLiteral("stopped");

    if (!readConfiguration(configPath)) {
        return false;
    }

    if (!QFileInfo::exists(m_qemuExecutable) || !QFileInfo(m_qemuExecutable).isFile()) {
        reportError(QStringLiteral("QEMU executable was not found at '%1'. Check qemuPath in the instance config or pass --qemu-path.")
                        .arg(QDir::toNativeSeparators(m_qemuExecutable)));
        return false;
    }
    if (!QFileInfo::exists(m_overlayImage) || !QFileInfo(m_overlayImage).isFile()) {
        reportError(QStringLiteral("The Android disk overlay was not found at '%1'. Create a qcow2 overlay from a bootable base image and update overlayImage.")
                        .arg(QDir::toNativeSeparators(m_overlayImage)));
        return false;
    }

    const QStringList arguments = buildCommandLine();
    qInfo().noquote() << "Starting QEMU:" << QDir::toNativeSeparators(m_qemuExecutable)
                      << arguments.join(QLatin1Char(' '));

    m_vmStatus = QStringLiteral("starting");
    m_starting = true;
    m_process.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_process.start(m_qemuExecutable, arguments, QIODevice::ReadOnly);
    if (!m_process.waitForStarted(10000)) {
        m_starting = false;
        if (!m_errorReported) {
            m_errorReported = true;
            reportError(QStringLiteral("Could not start QEMU at '%1': %2")
                            .arg(QDir::toNativeSeparators(m_qemuExecutable), m_process.errorString()));
        }
        m_vmStatus = QStringLiteral("stopped");
        return false;
    }
    m_starting = false;

    qInfo() << "QEMU process started with PID" << m_process.processId();
    m_qmpClient.connectToHost(QStringLiteral("localhost"), static_cast<quint16>(m_qmpPort));
    m_qmpTimeoutTimer.start(m_operationTimeoutMs);
    return true;
}

void QemuController::stop()
{
    if (!isRunning()) {
        return;
    }
    if (m_shutdownTimer.isActive()) {
        return;
    }
    if (!m_qmpClient.isConnected()) {
        reportError(QStringLiteral("QMP is not connected, so QEMU cannot receive a graceful shutdown request. Force-terminating QEMU."));
        kill();
        return;
    }

    m_vmStatus = QStringLiteral("stopping");
    m_qmpQuitRequested = false;
    m_shutdownTimer.start(m_operationTimeoutMs);
    if (sendCommand(QStringLiteral("system_powerdown")).isEmpty()) {
        reportError(QStringLiteral("Could not send the QMP system_powerdown request."));
        kill();
    }
}

void QemuController::kill()
{
    if (!isRunning()) {
        return;
    }

    m_qmpTimeoutTimer.stop();
    m_shutdownTimer.stop();
    m_forceKillRequested = true;
    m_vmStatus = QStringLiteral("stopping");
    m_qmpClient.disconnectFromHost();
    qWarning() << "Force-terminating QEMU process" << m_process.processId();
    m_process.kill();
}

bool QemuController::waitForExit(int timeoutMs)
{
    return !isRunning() || m_process.waitForFinished(timeoutMs);
}

QString QemuController::pause()
{
    return sendCommand(QStringLiteral("stop"));
}

QString QemuController::resume()
{
    return sendCommand(QStringLiteral("cont"));
}

QString QemuController::queryStatus()
{
    return sendCommand(QStringLiteral("query-status"));
}

QString QemuController::sendInputEvents(const QJsonArray& events)
{
    if (events.isEmpty()) {
        return {};
    }
    if (!m_qmpClient.isConnected()) {
        return {};
    }

    return sendCommand(QStringLiteral("input-send-event"),
                       {{QStringLiteral("events"), events}},
                       QStringLiteral("input-send-event"));
}

QString QemuController::saveSnapshot(const QString& name)
{
    return runDiskSnapshotCommand(QStringLiteral("save-snapshot"),
                                  QStringLiteral("-c"),
                                  name);
}

QString QemuController::loadSnapshot(const QString& name)
{
    return runDiskSnapshotCommand(QStringLiteral("load-snapshot"),
                                  QStringLiteral("-a"),
                                  name);
}

QString QemuController::runDiskSnapshotCommand(const QString& operation,
                                               const QString& option,
                                               const QString& name)
{
    static const QRegularExpression validName(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    if (!validName.match(name).hasMatch()) {
        reportError(QStringLiteral("Snapshot name must be 1-64 letters, numbers, dots, underscores, or hyphens."));
        return {};
    }
    if (isRunning()) {
        reportError(QStringLiteral("Disk-only snapshots require QEMU to be stopped."));
        return {};
    }
    if (m_snapshotProcess.state() != QProcess::NotRunning) {
        reportError(QStringLiteral("Another disk snapshot operation is already running."));
        return {};
    }
    if (m_overlayImage.isEmpty() || !QFileInfo(m_overlayImage).isFile()) {
        reportError(QStringLiteral("The configured qcow2 overlay is not available for snapshot operations."));
        return {};
    }

    const QString qemuImgExecutable = QFileInfo(m_qemuExecutable).dir().filePath(QStringLiteral("qemu-img.exe"));
    if (!QFileInfo(qemuImgExecutable).isFile()) {
        reportError(QStringLiteral("qemu-img.exe was not found next to the configured QEMU executable."));
        return {};
    }

    m_snapshotOperation = operation;
    m_snapshotOperationId = QStringLiteral("disk-snapshot-%1").arg(m_nextSnapshotOperationId++);
    m_snapshotProcess.setWorkingDirectory(QCoreApplication::applicationDirPath());
    m_snapshotProcess.start(qemuImgExecutable,
                            {QStringLiteral("snapshot"), option, name, m_overlayImage},
                            QIODevice::ReadOnly);
    return m_snapshotOperationId;
}

QString QemuController::getStatus() const
{
    return m_vmStatus;
}

int QemuController::adbHostPort() const
{
    return m_adbHostPort;
}

void QemuController::setQemuPath(const QString& path)
{
    m_qemuPathOverride = path.trimmed();
}

void QemuController::setOperationTimeoutMs(const int timeoutMs)
{
    if (timeoutMs < 1000 || timeoutMs > 300000) {
        reportError(QStringLiteral("Operation timeout must be between 1000 and 300000 milliseconds."));
        return;
    }
    m_operationTimeoutMs = timeoutMs;
}

bool QemuController::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

qint64 QemuController::processId() const
{
    return m_process.processId();
}

void QemuController::handleQmpConnected()
{
    if (!isRunning()) {
        return;
    }

    m_qmpTimeoutTimer.stop();
    m_vmStatus = QStringLiteral("running");
    emit qmpConnected();
    if (!m_vmStartedEmitted) {
        m_vmStartedEmitted = true;
        emit vmStarted();
    }
}

void QemuController::handleQmpResponse(const QString& id, const QJsonObject& result)
{
    const auto commandIt = m_pendingCommands.find(id);
    if (commandIt == m_pendingCommands.end()) {
        qWarning() << "Ignoring QMP response with unknown command ID" << id;
        return;
    }

    const QString operation = commandIt.value();
    m_pendingCommands.erase(commandIt);
    if (operation == QStringLiteral("query-status")) {
        const QString reportedStatus = result.value(QStringLiteral("status")).toString();
        if (!reportedStatus.isEmpty()) {
            m_vmStatus = reportedStatus;
        }
    }
    emit operationCompleted(id, operation, result);
}

void QemuController::handleQmpCommandFailure(const QString& id, const QString& message)
{
    const QString operation = m_pendingCommands.take(id);
    emit operationFailed(id, operation, message);
    reportError(QStringLiteral("QMP operation '%1' failed: %2").arg(operation, message));
}

void QemuController::handleQmpEvent(const QString& event, const QJsonObject& data)
{
    Q_UNUSED(data)

    if (event == QStringLiteral("STOP")) {
        m_vmStatus = QStringLiteral("paused");
    } else if (event == QStringLiteral("RESUME")) {
        m_vmStatus = QStringLiteral("running");
    } else if (event == QStringLiteral("SHUTDOWN")) {
        m_vmStatus = QStringLiteral("stopping");
    }
    qInfo() << "QMP event:" << event;
}

void QemuController::handleQmpError(const QString& message)
{
    if (m_shutdownTimer.isActive() && m_vmStatus == QStringLiteral("stopping")) {
        qInfo().noquote() << "Ignoring QMP disconnect while QEMU completes graceful shutdown:" << message;
        return;
    }

    if (m_qmpClient.isConnected()) {
        reportError(message);
    } else {
        qWarning().noquote() << message;
    }
}

void QemuController::handleQmpTimeout()
{
    if (!isRunning() || m_qmpClient.isConnected()) {
        return;
    }

    reportError(QStringLiteral("QMP did not connect to localhost:%1 within %2 ms. Check the QEMU process and port configuration.")
                    .arg(formatPort(m_qmpPort))
                    .arg(m_operationTimeoutMs));
    kill();
}

void QemuController::handleShutdownTimeout()
{
    if (!isRunning()) {
        return;
    }

    if (!m_qmpQuitRequested && m_qmpClient.isConnected()) {
        m_qmpQuitRequested = true;
        qWarning() << "Guest did not exit after system_powerdown; asking QEMU to quit cleanly.";
        if (!sendCommand(QStringLiteral("quit")).isEmpty()) {
            m_shutdownTimer.start(5000);
            return;
        }
    }

    reportError(QStringLiteral("QEMU did not exit after system_powerdown or QMP quit; force-terminating it."));
    kill();
}

void QemuController::handleProcessFinished(const int exitCode,
                                           const QProcess::ExitStatus exitStatus)
{
    handleStandardError();
    handleStandardOutput();
    m_qmpTimeoutTimer.stop();
    m_shutdownTimer.stop();
    m_qmpClient.disconnectFromHost();
    m_pendingCommands.clear();

    const bool forceKilled = m_forceKillRequested;
    if (!forceKilled && (!m_vmStartedEmitted || exitCode != 0 || exitStatus == QProcess::CrashExit)) {
        reportError(friendlyQemuExitMessage(exitCode, exitStatus));
    }

    m_vmStatus = QStringLiteral("stopped");
    m_vmStartedEmitted = false;
    m_starting = false;
    m_forceKillRequested = false;
    m_qmpQuitRequested = false;
    emit vmStopped(forceKilled ? -1 : exitCode);
}

void QemuController::handleProcessError(const QProcess::ProcessError error)
{
    const QString message = friendlyProcessError(error);
    if (error == QProcess::FailedToStart && !m_errorReported) {
        m_errorReported = true;
        reportError(message);
    } else if (error != QProcess::FailedToStart) {
        qWarning().noquote() << message;
    }
}

void QemuController::handleSnapshotProcessFinished(const int exitCode,
                                                  const QProcess::ExitStatus exitStatus)
{
    if (m_snapshotOperationId.isEmpty()) {
        return;
    }

    const QString id = m_snapshotOperationId;
    const QString operation = m_snapshotOperation;
    const QString standardOutput = QString::fromLocal8Bit(m_snapshotProcess.readAllStandardOutput()).trimmed();
    const QString standardError = QString::fromLocal8Bit(m_snapshotProcess.readAllStandardError()).trimmed();
    m_snapshotOperationId.clear();
    m_snapshotOperation.clear();

    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        emit operationCompleted(id, operation,
                                {{QStringLiteral("value"), standardOutput}});
        return;
    }

    QString message = standardError;
    if (message.isEmpty()) {
        message = standardOutput;
    }
    if (message.isEmpty()) {
        message = QStringLiteral("qemu-img exited with code %1.").arg(exitCode);
    }
    emit operationFailed(id, operation, message);
}

void QemuController::handleSnapshotProcessError(const QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || m_snapshotOperationId.isEmpty()) {
        return;
    }

    const QString id = m_snapshotOperationId;
    const QString operation = m_snapshotOperation;
    const QString message = QStringLiteral("Could not start qemu-img: %1")
                                .arg(m_snapshotProcess.errorString());
    m_snapshotOperationId.clear();
    m_snapshotOperation.clear();
    emit operationFailed(id, operation, message);
}

void QemuController::handleStandardError()
{
    const QByteArray data = m_process.readAllStandardError();
    if (data.isEmpty()) {
        return;
    }
    m_standardError.append(data);
    constexpr qsizetype maxDiagnosticBytes = 256 * 1024;
    if (m_standardError.size() > maxDiagnosticBytes) {
        m_standardError.remove(0, m_standardError.size() - maxDiagnosticBytes);
    }
    const QString text = QString::fromLocal8Bit(data).trimmed();
    if (!text.isEmpty()) {
        qWarning().noquote() << "QEMU:" << text;
    }
}

void QemuController::handleStandardOutput()
{
    const QByteArray data = m_process.readAllStandardOutput();
    if (data.isEmpty()) {
        return;
    }
    m_standardOutput.append(data);
    constexpr qsizetype maxDiagnosticBytes = 256 * 1024;
    if (m_standardOutput.size() > maxDiagnosticBytes) {
        m_standardOutput.remove(0, m_standardOutput.size() - maxDiagnosticBytes);
    }

    const QString text = QString::fromLocal8Bit(data).trimmed();
    if (!text.isEmpty()) {
        qInfo().noquote() << "QEMU serial/monitor:" << text;
    }
}

bool QemuController::readConfiguration(const QString& configPath)
{
    const QString resolvedConfigPath = resolveRuntimePath(configPath);
    QFile configFile(resolvedConfigPath);
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        reportError(QStringLiteral("Could not open instance config '%1': %2")
                        .arg(QDir::toNativeSeparators(resolvedConfigPath), configFile.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reportError(QStringLiteral("Instance config '%1' is not a valid JSON object: %2")
                        .arg(QDir::toNativeSeparators(resolvedConfigPath), parseError.errorString()));
        return false;
    }

    const QJsonObject config = document.object();
    m_cpuCores = config.value(QStringLiteral("cpuCores")).toInt(-1);
    m_ramMb = config.value(QStringLiteral("ramMB")).toInt(-1);
    m_qmpPort = config.value(QStringLiteral("qmpPort")).toInt(4444);
    m_adbHostPort = config.value("network").toObject().value("adbHostPort").toInt(
        config.value(QStringLiteral("adbHostPort")).toInt(5555));
    const auto display = config.value("display").toObject();
    m_displayWidth = display.value("width").toInt(1080);
    m_displayHeight = display.value("height").toInt(1920);
    const auto camera = config.value("camera").toObject();
    m_cameraMode = camera.value("mode").toString("none");
    m_cameraVendor = camera.value("vendorId").toString();
    m_cameraProduct = camera.value("productId").toString();
    if (m_displayWidth < 320 || m_displayWidth > 4096 || m_displayHeight < 320 || m_displayHeight > 4096) {
        reportError("Display width and height must be between 320 and 4096 pixels."); return false;
    }
    const auto validUsbId = [](const QString& id) {
        if (id.size() != 6 || !id.startsWith("0x")) return false;
        bool ok = false; id.mid(2).toUInt(&ok, 16); return ok;
    };
    if ((m_cameraMode != "none" && m_cameraMode != "webcam")
        || (m_cameraMode == "webcam" && (!validUsbId(m_cameraVendor) || !validUsbId(m_cameraProduct)))) {
        reportError("Camera mode must be None or USB webcam with valid 0xNNNN vendor/product IDs. Test pattern is unavailable."); return false;
    }
    const QString configDirectory = QFileInfo(resolvedConfigPath).absolutePath();

    const QString configuredQemuPath = m_qemuPathOverride.isEmpty()
        ? config.value(QStringLiteral("qemuPath")).toString()
        : m_qemuPathOverride;
    const QString configuredOverlay = config.value(QStringLiteral("overlayImage")).toString();

    if (m_cpuCores <= 0 || m_ramMb <= 0) {
        reportError(QStringLiteral("Instance config must set cpuCores and ramMB to positive integers."));
        return false;
    }
    if (!validPort(m_qmpPort) || !validPort(m_adbHostPort)) {
        reportError(QStringLiteral("Instance config qmpPort and adbHostPort must be between 1 and 65535."));
        return false;
    }
    if (configuredQemuPath.trimmed().isEmpty() || configuredOverlay.trimmed().isEmpty()) {
        reportError(QStringLiteral("Instance config must provide qemuPath and overlayImage."));
        return false;
    }

    m_qemuExecutable = resolveRuntimePath(configuredQemuPath, configDirectory);
    m_overlayImage = resolveRuntimePath(configuredOverlay, configDirectory);
    return true;
}

QString QemuController::resolveRuntimePath(const QString& path,
                                           const QString& configDirectory) const
{
    const QFileInfo pathInfo(path);
    if (pathInfo.isAbsolute()) {
        return normalizedAbsolutePath(path);
    }

    const QStringList baseDirectories{
        QCoreApplication::applicationDirPath(),
        QDir::currentPath(),
        configDirectory,
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../.."))
    };
    for (const QString& baseDirectory : baseDirectories) {
        if (baseDirectory.isEmpty()) {
            continue;
        }
        const QString candidate = QDir(baseDirectory).absoluteFilePath(path);
        if (QFileInfo::exists(candidate)) {
            return normalizedAbsolutePath(candidate);
        }
    }

    return normalizedAbsolutePath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path));
}

QStringList QemuController::buildCommandLine() const
{
    QStringList arguments;
    arguments << QStringLiteral("-accel")
              << QStringLiteral("whpx,kernel-irqchip=off");
    arguments << QStringLiteral("-m") << QString::number(m_ramMb);
    arguments << QStringLiteral("-smp") << QString::number(m_cpuCores);
    arguments << QStringLiteral("-cpu") << QStringLiteral("max");
    arguments << QStringLiteral("-drive")
              << QStringLiteral("file=%1,format=qcow2,if=virtio")
                     .arg(QDir::fromNativeSeparators(m_overlayImage).replace(",", ",,"));
    arguments << QStringLiteral("-device") << QStringLiteral("usb-ehci");
    arguments << QStringLiteral("-device") << QStringLiteral("usb-tablet");
    arguments << QStringLiteral("-netdev")
              << QStringLiteral("user,id=net0,hostfwd=tcp::%1-:5555").arg(m_adbHostPort);
    arguments << QStringLiteral("-device") << QStringLiteral("virtio-net-pci,netdev=net0");
    // Bliss OS 14 needs the OpenGL-capable virtio VGA device; standard VGA stalls at its boot splash.
    arguments << QStringLiteral("-vga") << QStringLiteral("none");
    arguments << QStringLiteral("-device") << QStringLiteral("virtio-vga-gl,xres=%1,yres=%2").arg(m_displayWidth).arg(m_displayHeight);
    if (m_cameraMode == "webcam")
        arguments << "-device" << QString("usb-host,vendorid=%1,productid=%2").arg(m_cameraVendor, m_cameraProduct);
    arguments << QStringLiteral("-display") << QStringLiteral("sdl,gl=on");
    arguments << QStringLiteral("-qmp")
              << QStringLiteral("tcp:localhost:%1,server,wait=off").arg(m_qmpPort);
    arguments << QStringLiteral("-serial") << QStringLiteral("mon:stdio");
    arguments << QStringLiteral("-boot") << QStringLiteral("c");
    return arguments;
}

QString QemuController::sendCommand(const QString& command,
                                    const QJsonObject& arguments,
                                    const QString& operationName)
{
    if (!m_qmpClient.isConnected()) {
        reportError(QStringLiteral("Cannot run '%1': QMP is not connected.").arg(command));
        return {};
    }

    const QString id = m_qmpClient.execute(command, arguments);
    if (!id.isEmpty()) {
        m_pendingCommands.insert(id, operationName.isEmpty() ? command : operationName);
    }
    return id;
}

void QemuController::reportError(const QString& message)
{
    qCritical().noquote() << message;
    emit vmError(message);
}

QString QemuController::friendlyProcessError(const QProcess::ProcessError error) const
{
    switch (error) {
    case QProcess::FailedToStart:
        return QStringLiteral("QEMU could not be started at '%1'. Check that the executable exists and that Windows permits it to run.")
            .arg(QDir::toNativeSeparators(m_qemuExecutable));
    case QProcess::Crashed:
        return QStringLiteral("QEMU crashed while the VM was running.");
    case QProcess::Timedout:
        return QStringLiteral("A QEMU process operation timed out.");
    case QProcess::WriteError:
        return QStringLiteral("Could not write to the QEMU process.");
    case QProcess::ReadError:
        return QStringLiteral("Could not read from the QEMU process.");
    case QProcess::UnknownError:
        return QStringLiteral("QEMU reported an unknown process error: %1").arg(m_process.errorString());
    }
    return QStringLiteral("QEMU reported an unrecognized process error.");
}

QString QemuController::friendlyQemuExitMessage(const int exitCode,
                                                const QProcess::ExitStatus exitStatus) const
{
    const QString stderrText = QString::fromLocal8Bit(m_standardError).trimmed();
    if (stderrText.contains(QStringLiteral("WHPX: Failed to initialize partition"), Qt::CaseInsensitive)) {
        return QStringLiteral("WHPX could not initialize. Enable CPU virtualization in BIOS and the Windows Hypervisor Platform feature. QEMU said: %1")
            .arg(stderrText);
    }
    if (stderrText.contains(QStringLiteral("Address already in use"), Qt::CaseInsensitive)) {
        return QStringLiteral("A configured QEMU port is already in use. Change qmpPort or adbHostPort. QEMU said: %1")
            .arg(stderrText);
    }
    if (stderrText.contains(QStringLiteral("Could not open"), Qt::CaseInsensitive)) {
        return QStringLiteral("QEMU could not open the configured disk overlay. Check overlayImage and its backing image. QEMU said: %1")
            .arg(stderrText);
    }

    const QString exitDescription = exitStatus == QProcess::CrashExit
        ? QStringLiteral("crashed")
        : QStringLiteral("exited with code %1").arg(exitCode);
    return stderrText.isEmpty()
        ? QStringLiteral("QEMU %1 before the VM completed startup.").arg(exitDescription)
        : QStringLiteral("QEMU %1. QEMU said: %2").arg(exitDescription, stderrText);
}
