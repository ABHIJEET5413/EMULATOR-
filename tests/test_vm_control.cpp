#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "qemu_controller.h"

#include <cstdio>
#include <utility>

class VmControlTestRunner final : public QObject {
public:
    VmControlTestRunner(QCoreApplication& application,
                        QString configPath,
                        QString qemuPath,
                        QString selectedTest,
                        const int timeoutMs,
                        const int bootWaitMs)
        : m_application(application)
        , m_configPath(std::move(configPath))
        , m_qemuPath(std::move(qemuPath))
        , m_selectedTest(std::move(selectedTest))
        , m_timeoutMs(timeoutMs)
        , m_bootWaitMs(bootWaitMs)
    {
        m_runAll = m_selectedTest == QStringLiteral("all");
        m_controller.setOperationTimeoutMs(m_timeoutMs);
        if (!m_qemuPath.isEmpty()) {
            m_controller.setQemuPath(m_qemuPath);
        }

        m_operationTimer.setSingleShot(true);
        connect(&m_operationTimer, &QTimer::timeout, this, [this]() {
            handleOperationTimeout();
        });

        connect(&m_whpxProcess,
                qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                this,
                [this](const int exitCode, QProcess::ExitStatus) {
                    handleWhpxFinished(exitCode);
                });
        connect(&m_whpxProcess, &QProcess::errorOccurred,
                this, [this](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart && m_stage == Stage::WaitWhpx) {
                        fail(QStringLiteral("whpx_check"),
                             QStringLiteral("Could not start PowerShell to run check-whpx.ps1."));
                    }
                });

        connect(&m_controller, &QemuController::qmpConnected,
                this, [this]() { handleQmpConnected(); });
        connect(&m_controller, &QemuController::vmError,
                this, [this](const QString& message) {
                    fail(currentTestName(), message);
                });
        connect(&m_controller, &QemuController::vmStopped,
                this, [this](const int exitCode) { handleVmStopped(exitCode); });
        connect(&m_controller, &QemuController::operationCompleted,
                this,
                [this](const QString& id, const QString& operation, const QJsonObject& result) {
                    handleOperationCompleted(id, operation, result);
                });
        connect(&m_controller, &QemuController::operationFailed,
                this,
                [this](const QString& id, const QString& operation, const QString& message) {
                    handleOperationFailed(id, operation, message);
                });
    }

    void run()
    {
        if (!loadConfigForTest()) {
            return;
        }
        if (m_selectedTest == QStringLiteral("config_load")) {
            finish(0);
            return;
        }

        runWhpxCheck();
    }

private:
    enum class Stage {
        Ready,
        WaitWhpx,
        WaitQmp,
        WaitSnapshotBootQmp,
        WaitGuestBoot,
        WaitQueryStatus,
        WaitPause,
        WaitPauseStatus,
        WaitResume,
        WaitResumeStatus,
        WaitSnapshotSave,
        WaitSnapshotLoad,
        WaitGracefulStop,
        WaitForceKill,
        CleanupAfterLaunch,
        CleanupAfterSelectedTest,
        CleanupAfterFailure
    };

    QString resolveProjectPath(const QString& path) const
    {
        const QFileInfo pathInfo(path);
        if (pathInfo.isAbsolute()) {
            return QDir::cleanPath(pathInfo.absoluteFilePath());
        }

        const QStringList baseDirectories{
            QCoreApplication::applicationDirPath(),
            QDir::currentPath()
        };
        for (const QString& baseDirectory : baseDirectories) {
            const QString candidate = QDir(baseDirectory).absoluteFilePath(path);
            if (QFileInfo::exists(candidate)) {
                return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
            }
        }
        return QDir::cleanPath(QDir(QDir::currentPath()).absoluteFilePath(path));
    }

    bool loadConfigForTest()
    {
        m_resolvedConfigPath = resolveProjectPath(m_configPath);
        QFile configFile(m_resolvedConfigPath);
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fail(QStringLiteral("config_load"),
                 QStringLiteral("Could not open '%1': %2")
                     .arg(QDir::toNativeSeparators(m_resolvedConfigPath), configFile.errorString()));
            return false;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(QStringLiteral("config_load"),
                 QStringLiteral("Invalid JSON in '%1': %2")
                     .arg(QDir::toNativeSeparators(m_resolvedConfigPath), parseError.errorString()));
            return false;
        }

        const QJsonObject config = document.object();
        const int cpuCores = config.value(QStringLiteral("cpuCores")).toInt(-1);
        const int ramMb = config.value(QStringLiteral("ramMB")).toInt(-1);
        if (cpuCores <= 0 || ramMb <= 0
            || config.value(QStringLiteral("qemuPath")).toString().isEmpty()
            || config.value(QStringLiteral("overlayImage")).toString().isEmpty()) {
            fail(QStringLiteral("config_load"),
                 QStringLiteral("Config must contain positive cpuCores/ramMB and non-empty qemuPath/overlayImage fields."));
            return false;
        }

        printPass(QStringLiteral("config_load"),
                  QStringLiteral("Loaded config with %1 cores and %2 MB RAM.")
                      .arg(cpuCores)
                      .arg(ramMb));
        return true;
    }

    void runWhpxCheck()
    {
        const QString scriptPath = resolveProjectPath(QStringLiteral("vm/scripts/check-whpx.ps1"));
        if (!QFileInfo::exists(scriptPath)) {
            fail(QStringLiteral("whpx_check"),
                 QStringLiteral("WHPX check script was not found at '%1'.")
                     .arg(QDir::toNativeSeparators(scriptPath)));
            return;
        }

        QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        if (powershell.isEmpty()) {
            powershell = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
        }
        if (powershell.isEmpty()) {
            fail(QStringLiteral("whpx_check"),
                 QStringLiteral("PowerShell was not found. Install Windows PowerShell or PowerShell 7."));
            return;
        }

        m_stage = Stage::WaitWhpx;
        m_operationTimer.start(m_timeoutMs);
        m_whpxProcess.start(powershell,
                            {QStringLiteral("-NoProfile"),
                             QStringLiteral("-ExecutionPolicy"),
                             QStringLiteral("Bypass"),
                             QStringLiteral("-File"),
                             scriptPath},
                            QIODevice::ReadOnly);
        if (!m_whpxProcess.waitForStarted(5000)) {
            m_operationTimer.stop();
            fail(QStringLiteral("whpx_check"),
                 QStringLiteral("Could not run check-whpx.ps1: %1").arg(m_whpxProcess.errorString()));
        }
    }

    void handleWhpxFinished(const int exitCode)
    {
        if (m_stage != Stage::WaitWhpx || m_finishing) {
            return;
        }
        m_operationTimer.stop();

        const QString output = QString::fromLocal8Bit(m_whpxProcess.readAllStandardOutput()).trimmed();
        const QString errors = QString::fromLocal8Bit(m_whpxProcess.readAllStandardError()).trimmed();
        if (exitCode != 0) {
            QString detail = output;
            if (!errors.isEmpty()) {
                detail += (detail.isEmpty() ? QString() : QStringLiteral("\n")) + errors;
            }
            if (detail.isEmpty()) {
                detail = QStringLiteral("check-whpx.ps1 exited with code %1.").arg(exitCode);
            }
            fail(QStringLiteral("whpx_check"), detail);
            return;
        }

        printPass(QStringLiteral("whpx_check"), output.isEmpty()
                      ? QStringLiteral("WHPX check completed successfully.")
                      : output);
        if (m_selectedTest == QStringLiteral("whpx_check")) {
            finish(0);
            return;
        }

        startVm();
    }

    void startVm(const bool snapshotValidation = false)
    {
        m_stage = snapshotValidation ? Stage::WaitSnapshotBootQmp : Stage::WaitQmp;
        if (!m_controller.start(m_resolvedConfigPath)) {
            if (!m_finishing) {
                fail(QStringLiteral("qemu_launch"), QStringLiteral("QemuController could not start QEMU."));
            }
            return;
        }

        printPass(QStringLiteral("qemu_launch"), QStringLiteral("QEMU process started."));
        if (m_selectedTest == QStringLiteral("qemu_launch")) {
            m_stage = Stage::CleanupAfterLaunch;
            m_operationTimer.start(10000);
            m_controller.kill();
            return;
        }
        m_operationTimer.start(m_timeoutMs);
    }

    void handleQmpConnected()
    {
        if (m_finishing) {
            return;
        }

        if (m_stage == Stage::WaitSnapshotBootQmp) {
            m_operationTimer.stop();
            printPass(QStringLiteral("snapshot"),
                      QStringLiteral("Restored disk snapshot '%1' and reconnected to QMP.").arg(m_snapshotName));
            if (m_runAll) {
                beginForceKill();
            } else {
                cleanupAfterSelectedTest();
            }
            return;
        }
        if (m_stage != Stage::WaitQmp) {
            return;
        }

        m_operationTimer.stop();
        printPass(QStringLiteral("qmp_connect"), QStringLiteral("Connected to QMP."));

        if (m_runAll || m_selectedTest == QStringLiteral("query_status")) {
            beginQueryStatus();
        } else if (m_selectedTest == QStringLiteral("pause_resume")) {
            beginPauseResume();
        } else if (m_selectedTest == QStringLiteral("snapshot")) {
            scheduleGracefulStop();
        } else if (m_selectedTest == QStringLiteral("graceful_stop")) {
            scheduleGracefulStop();
        } else if (m_selectedTest == QStringLiteral("force_kill")) {
            beginForceKill();
        } else {
            cleanupAfterSelectedTest();
        }
    }

    void beginQueryStatus()
    {
        m_stage = Stage::WaitQueryStatus;
        m_expectedCommandId = m_controller.queryStatus();
        if (m_expectedCommandId.isEmpty()) {
            fail(QStringLiteral("query_status"), QStringLiteral("Could not send query-status."));
            return;
        }
        m_operationTimer.start(m_timeoutMs);
    }

    void beginPauseResume()
    {
        m_stage = Stage::WaitPause;
        m_expectedCommandId = m_controller.pause();
        if (m_expectedCommandId.isEmpty()) {
            fail(QStringLiteral("pause_resume"), QStringLiteral("Could not send QMP stop."));
            return;
        }
        m_operationTimer.start(m_timeoutMs);
    }

    void beginSnapshot()
    {
        m_snapshotName = QStringLiteral("droidbox_phase1_%1")
                             .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmsszzz")));
        m_stage = Stage::WaitSnapshotSave;
        m_expectedCommandId = m_controller.saveSnapshot(m_snapshotName);
        if (m_expectedCommandId.isEmpty()) {
            fail(QStringLiteral("snapshot"), QStringLiteral("Could not start the disk snapshot operation."));
            return;
        }
        m_operationTimer.start(m_timeoutMs);
    }

    void beginGracefulStop()
    {
        m_stage = Stage::WaitGracefulStop;
        m_operationTimer.start(m_timeoutMs + 15000);
        m_controller.stop();
    }

    void scheduleGracefulStop()
    {
        m_stage = Stage::WaitGuestBoot;
        QTextStream(stdout) << "[INFO] Waiting " << m_bootWaitMs
                            << " ms for the guest to boot before system_powerdown."
                            << Qt::endl;
        QTimer::singleShot(m_bootWaitMs, this, [this]() {
            if (!m_finishing && m_stage == Stage::WaitGuestBoot) {
                beginGracefulStop();
            }
        });
    }

    void beginForceKill()
    {
        m_stage = Stage::WaitForceKill;
        m_operationTimer.start(10000);
        m_controller.kill();
    }

    void cleanupAfterSelectedTest()
    {
        if (!m_controller.isRunning()) {
            finish(0);
            return;
        }
        m_stage = Stage::CleanupAfterSelectedTest;
        m_operationTimer.start(10000);
        m_controller.kill();
    }

    void handleOperationCompleted(const QString& id,
                                 const QString& operation,
                                 const QJsonObject& result)
    {
        if (m_finishing || id != m_expectedCommandId) {
            return;
        }
        m_operationTimer.stop();
        m_expectedCommandId.clear();

        if (m_stage == Stage::WaitQueryStatus) {
            const QString status = result.value(QStringLiteral("status")).toString();
            if (status != QStringLiteral("running") && status != QStringLiteral("paused")) {
                fail(QStringLiteral("query_status"),
                     QStringLiteral("QEMU returned unexpected status '%1'.").arg(status));
                return;
            }
            printPass(QStringLiteral("query_status"),
                      QStringLiteral("VM status is '%1'.").arg(status));
            if (m_runAll) {
                beginPauseResume();
            } else {
                cleanupAfterSelectedTest();
            }
            return;
        }

        if (m_stage == Stage::WaitPause) {
            m_stage = Stage::WaitPauseStatus;
            m_expectedCommandId = m_controller.queryStatus();
            if (m_expectedCommandId.isEmpty()) {
                fail(QStringLiteral("pause_resume"), QStringLiteral("Could not query status after pausing."));
                return;
            }
            m_operationTimer.start(m_timeoutMs);
            return;
        }

        if (m_stage == Stage::WaitPauseStatus) {
            const QString status = result.value(QStringLiteral("status")).toString();
            if (status != QStringLiteral("paused")) {
                fail(QStringLiteral("pause_resume"),
                     QStringLiteral("Expected paused state, but QEMU reported '%1'.").arg(status));
                return;
            }
            m_stage = Stage::WaitResume;
            m_expectedCommandId = m_controller.resume();
            if (m_expectedCommandId.isEmpty()) {
                fail(QStringLiteral("pause_resume"), QStringLiteral("Could not send QMP cont."));
                return;
            }
            m_operationTimer.start(m_timeoutMs);
            return;
        }

        if (m_stage == Stage::WaitResume) {
            m_stage = Stage::WaitResumeStatus;
            m_expectedCommandId = m_controller.queryStatus();
            if (m_expectedCommandId.isEmpty()) {
                fail(QStringLiteral("pause_resume"), QStringLiteral("Could not query status after resuming."));
                return;
            }
            m_operationTimer.start(m_timeoutMs);
            return;
        }

        if (m_stage == Stage::WaitResumeStatus) {
            const QString status = result.value(QStringLiteral("status")).toString();
            if (status != QStringLiteral("running")) {
                fail(QStringLiteral("pause_resume"),
                     QStringLiteral("Expected running state, but QEMU reported '%1'.").arg(status));
                return;
            }
            printPass(QStringLiteral("pause_resume"), QStringLiteral("Pause and resume both changed VM state."));
            if (m_runAll) {
                scheduleGracefulStop();
            } else {
                cleanupAfterSelectedTest();
            }
            return;
        }

        if (m_stage == Stage::WaitSnapshotSave) {
            const QString output = result.value(QStringLiteral("value")).toString();
            if (output.startsWith(QStringLiteral("Error:"), Qt::CaseInsensitive)
                || output.startsWith(QStringLiteral("Could not"), Qt::CaseInsensitive)) {
                fail(QStringLiteral("snapshot"), output);
                return;
            }
            m_stage = Stage::WaitSnapshotLoad;
            m_expectedCommandId = m_controller.loadSnapshot(m_snapshotName);
            if (m_expectedCommandId.isEmpty()) {
                fail(QStringLiteral("snapshot"), QStringLiteral("Could not start the disk snapshot restore."));
                return;
            }
            m_operationTimer.start(m_timeoutMs);
            return;
        }

        if (m_stage == Stage::WaitSnapshotLoad) {
            Q_UNUSED(result)
            startVm(true);
            return;
        }

        Q_UNUSED(operation)
    }

    void handleOperationFailed(const QString& id,
                               const QString& operation,
                               const QString& message)
    {
        if (!m_finishing && id == m_expectedCommandId) {
            fail(currentTestName(),
                 QStringLiteral("QMP operation '%1' failed: %2").arg(operation, message));
        }
    }

    void handleVmStopped(const int exitCode)
    {
        m_operationTimer.stop();
        if (m_finishing) {
            finish(1);
            return;
        }

        if (m_stage == Stage::CleanupAfterLaunch || m_stage == Stage::CleanupAfterSelectedTest) {
            finish(0);
            return;
        }
        if (m_stage == Stage::CleanupAfterFailure) {
            finish(1);
            return;
        }
        if (m_stage == Stage::WaitGuestBoot) {
            fail(QStringLiteral("graceful_stop"), QStringLiteral("QEMU exited before the guest boot wait completed."));
            return;
        }
        if (m_stage == Stage::WaitGracefulStop) {
            if (exitCode == -1) {
                fail(QStringLiteral("graceful_stop"), QStringLiteral("QEMU required force termination."));
                return;
            }
            printPass(QStringLiteral("graceful_stop"), QStringLiteral("QEMU exited after the shutdown request."));
            if (m_runAll || m_selectedTest == QStringLiteral("snapshot")) {
                beginSnapshot();
            } else {
                finish(0);
            }
            return;
        }
        if (m_stage == Stage::WaitForceKill) {
            if (exitCode != -1) {
                fail(QStringLiteral("force_kill"), QStringLiteral("QEMU did not report a controller-requested force kill."));
                return;
            }
            printPass(QStringLiteral("force_kill"), QStringLiteral("QEMU process was force-terminated."));
            finish(0);
        }
    }

    void handleOperationTimeout()
    {
        if (m_finishing) {
            finish(1);
            return;
        }

        if (m_stage == Stage::WaitQmp) {
            fail(QStringLiteral("qmp_connect"),
                 QStringLiteral("QMP did not connect within %1 ms.").arg(m_timeoutMs));
        } else if (m_stage == Stage::WaitGracefulStop) {
            fail(QStringLiteral("graceful_stop"),
                 QStringLiteral("QEMU did not shut down within %1 ms.").arg(m_timeoutMs));
        } else if (m_stage == Stage::WaitWhpx) {
            fail(QStringLiteral("whpx_check"),
                 QStringLiteral("check-whpx.ps1 did not finish within %1 ms.").arg(m_timeoutMs));
        } else {
            fail(currentTestName(),
                 QStringLiteral("The operation timed out after %1 ms.").arg(m_timeoutMs));
        }
    }

    QString currentTestName() const
    {
        switch (m_stage) {
        case Stage::WaitWhpx:
            return QStringLiteral("whpx_check");
        case Stage::WaitSnapshotBootQmp:
            return QStringLiteral("snapshot");
        case Stage::WaitQmp:
            return QStringLiteral("qmp_connect");
        case Stage::WaitGuestBoot:
            return QStringLiteral("graceful_stop");
        case Stage::WaitQueryStatus:
            return QStringLiteral("query_status");
        case Stage::WaitPause:
        case Stage::WaitPauseStatus:
        case Stage::WaitResume:
        case Stage::WaitResumeStatus:
            return QStringLiteral("pause_resume");
        case Stage::WaitSnapshotSave:
        case Stage::WaitSnapshotLoad:
            return QStringLiteral("snapshot");
        case Stage::WaitGracefulStop:
            return QStringLiteral("graceful_stop");
        case Stage::WaitForceKill:
            return QStringLiteral("force_kill");
        case Stage::CleanupAfterLaunch:
            return QStringLiteral("qemu_launch");
        default:
            return m_selectedTest;
        }
    }

    void fail(const QString& test, const QString& message)
    {
        if (m_finishing || m_completed) {
            return;
        }
        m_finishing = true;
        m_operationTimer.stop();
        m_expectedCommandId.clear();
        QTextStream(stderr) << "[FAIL] " << test << ": " << message << Qt::endl;

        if (m_whpxProcess.state() != QProcess::NotRunning) {
            m_whpxProcess.kill();
        }
        if (m_controller.isRunning()) {
            m_stage = Stage::CleanupAfterFailure;
            m_operationTimer.start(10000);
            m_controller.kill();
            return;
        }
        finish(1);
    }

    void printPass(const QString& test, const QString& message)
    {
        QTextStream(stdout) << "[PASS] " << test << ": " << message << Qt::endl;
    }

    void finish(const int exitCode)
    {
        if (m_completed) {
            return;
        }
        m_completed = true;
        m_operationTimer.stop();
        if (m_whpxProcess.state() != QProcess::NotRunning) {
            m_whpxProcess.kill();
        }
        m_application.exit(exitCode);
    }

    QCoreApplication& m_application;
    QemuController m_controller;
    QProcess m_whpxProcess;
    QTimer m_operationTimer;
    QString m_configPath;
    QString m_resolvedConfigPath;
    QString m_qemuPath;
    QString m_selectedTest;
    QString m_expectedCommandId;
    QString m_snapshotName;
    Stage m_stage{Stage::Ready};
    int m_timeoutMs{30000};
    int m_bootWaitMs{60000};
    bool m_runAll{true};
    bool m_finishing{false};
    bool m_completed{false};
};

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_vm_control"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DroidBox Phase 1 VM control CLI harness"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption configOption(
        {QStringLiteral("c"), QStringLiteral("config")},
        QStringLiteral("Instance config JSON path."),
        QStringLiteral("path"),
        QStringLiteral("vm/configs/default-instance.json"));
    const QCommandLineOption qemuPathOption(
        QStringLiteral("qemu-path"),
        QStringLiteral("Override qemuPath from the config."),
        QStringLiteral("path"));
    const QCommandLineOption testOption(
        QStringLiteral("test"),
        QStringLiteral("Run one named test or all tests."),
        QStringLiteral("name"),
        QStringLiteral("all"));
    const QCommandLineOption timeoutOption(
        QStringLiteral("timeout"),
        QStringLiteral("Timeout in milliseconds for each operation (default: 30000)."),
        QStringLiteral("ms"),
        QStringLiteral("30000"));
    const QCommandLineOption bootWaitOption(
        QStringLiteral("boot-wait"),
        QStringLiteral("Wait in milliseconds before testing graceful shutdown (default: 60000)."),
        QStringLiteral("ms"),
        QStringLiteral("60000"));
    parser.addOption(configOption);
    parser.addOption(qemuPathOption);
    parser.addOption(testOption);
    parser.addOption(timeoutOption);
    parser.addOption(bootWaitOption);
    parser.process(application);

    const QString selectedTest = parser.value(testOption).trimmed().toLower();
    const QStringList testNames{
        QStringLiteral("all"),
        QStringLiteral("config_load"),
        QStringLiteral("whpx_check"),
        QStringLiteral("qemu_launch"),
        QStringLiteral("qmp_connect"),
        QStringLiteral("query_status"),
        QStringLiteral("pause_resume"),
        QStringLiteral("snapshot"),
        QStringLiteral("graceful_stop"),
        QStringLiteral("force_kill")
    };
    if (!testNames.contains(selectedTest)) {
        QTextStream(stderr) << "Unknown test '" << selectedTest << "'. Available tests: "
                            << testNames.join(QStringLiteral(", ")) << Qt::endl;
        return 2;
    }

    bool timeoutOk = false;
    const int timeoutMs = parser.value(timeoutOption).toInt(&timeoutOk);
    if (!timeoutOk || timeoutMs < 1000 || timeoutMs > 300000) {
        QTextStream(stderr) << "--timeout must be between 1000 and 300000 milliseconds." << Qt::endl;
        return 2;
    }

    bool bootWaitOk = false;
    const int bootWaitMs = parser.value(bootWaitOption).toInt(&bootWaitOk);
    if (!bootWaitOk || bootWaitMs < 0 || bootWaitMs > 300000) {
        QTextStream(stderr) << "--boot-wait must be between 0 and 300000 milliseconds." << Qt::endl;
        return 2;
    }

    VmControlTestRunner runner(application,
                               parser.value(configOption),
                               parser.value(qemuPathOption),
                               selectedTest,
                               timeoutMs,
                               bootWaitMs);
    QTimer::singleShot(0, &runner, [&runner]() { runner.run(); });
    return application.exec();
}
