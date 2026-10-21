#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "qmp_client.h"

/**
 * @file qemu_controller.h
 * @brief Launches QEMU with WHPX and exposes VM controls over QMP.
 */
class QemuController final : public QObject {
    Q_OBJECT

public:
    /** Create a controller for one QEMU process. */
    explicit QemuController(QObject* parent = nullptr);

    /** Disconnect QMP and stop QEMU if the controller is destroyed while it is running. */
    ~QemuController() override;

    /** Read configuration and start QEMU. Returns false if validation or process start fails. */
    bool start(const QString& configPath);

    /** Request guest ACPI shutdown; ask QEMU to quit if the guest ignores it, then force terminate if needed. */
    void stop();

    /** Force-terminate the QEMU process. */
    void kill();

    /** Send QMP stop (pause), cont (resume), or query-status. */
    QString pause();
    QString resume();
    QString queryStatus();

    /** Forward input events to QEMU while QMP is connected. */
    QString sendInputEvents(const QJsonArray& events);

    /** Save or load a qcow2 disk-only snapshot. QEMU must be stopped. */
    QString saveSnapshot(const QString& name);
    QString loadSnapshot(const QString& name);

    /** Return the most recently observed VM state. */
    QString getStatus() const;

    /** Return the host TCP port forwarded to Android's ADB port. */
    int adbHostPort() const;

    /** Override qemuPath from config; used by the CLI harness and manual runs. */
    void setQemuPath(const QString& path);

    /** Set QMP-connect and graceful-shutdown timeouts. The default is 30000 ms. */
    void setOperationTimeoutMs(int timeoutMs);

    /** Return whether the child QEMU process is currently running. */
    bool isRunning() const;

    /** Return the child QEMU process ID, or zero when it has not started. */
    qint64 processId() const;

signals:
    /** Emitted after QEMU launches and QMP completes its handshake. */
    void vmStarted();
    /** Emitted when QEMU exits; -1 denotes a controller-requested force kill. */
    void vmStopped(int exitCode);
    /** Emitted for configuration, QEMU, and QMP failures. */
    void vmError(const QString& message);
    /** Emitted after the QMP handshake completes. */
    void qmpConnected();
    /** Emitted for successful asynchronous QMP operations. */
    void operationCompleted(const QString& id,
                            const QString& operation,
                            const QJsonObject& result);
    /** Emitted for failed asynchronous QMP operations. */
    void operationFailed(const QString& id,
                         const QString& operation,
                         const QString& message);

private slots:
    void handleQmpConnected();
    void handleQmpResponse(const QString& id, const QJsonObject& result);
    void handleQmpCommandFailure(const QString& id, const QString& message);
    void handleQmpEvent(const QString& event, const QJsonObject& data);
    void handleQmpError(const QString& message);
    void handleQmpTimeout();
    void handleShutdownTimeout();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void handleSnapshotProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleSnapshotProcessError(QProcess::ProcessError error);
    void handleStandardError();
    void handleStandardOutput();

private:
    bool readConfiguration(const QString& configPath);
    QString resolveRuntimePath(const QString& path, const QString& configDirectory = {}) const;
    QStringList buildCommandLine() const;
    QString sendCommand(const QString& command,
                        const QJsonObject& arguments = {},
                        const QString& operationName = {});
    QString runDiskSnapshotCommand(const QString& operation,
                                   const QString& option,
                                   const QString& name);
    void reportError(const QString& message);
    QString friendlyProcessError(QProcess::ProcessError error) const;
    QString friendlyQemuExitMessage(int exitCode, QProcess::ExitStatus exitStatus) const;

    QProcess m_process;
    QProcess m_snapshotProcess;
    QmpClient m_qmpClient;
    QTimer m_qmpTimeoutTimer;
    QTimer m_shutdownTimer;
    QHash<QString, QString> m_pendingCommands;
    QByteArray m_standardError;
    QByteArray m_standardOutput;
    QString m_qemuPathOverride;
    QString m_qemuExecutable;
    QString m_overlayImage;
    QString m_snapshotOperationId;
    QString m_snapshotOperation;
    QString m_vmStatus{"stopped"};
    int m_cpuCores{0};
    int m_ramMb{0};
    int m_qmpPort{4444};
    int m_adbHostPort{5555};
    int m_operationTimeoutMs{30000};
    quint64 m_nextSnapshotOperationId{1};
    bool m_starting{false};
    bool m_vmStartedEmitted{false};
    bool m_forceKillRequested{false};
    bool m_qmpQuitRequested{false};
    bool m_errorReported{false};
};
