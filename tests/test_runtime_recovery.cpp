#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "performance_monitor.h"
#include "runtime_recovery.h"

namespace {

int failures = 0;

void check(const bool condition, const QString& name)
{
    QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!condition) {
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), QStringLiteral("temporary recovery directory is available"));
    if (!temporary.isValid()) {
        return 1;
    }

    RuntimeRecovery recovery(temporary.filePath(QStringLiteral("markers")));
    QString errorMessage;
    check(recovery.beginSession(QStringLiteral("active"), QCoreApplication::applicationPid(), &errorMessage),
          QStringLiteral("records an active session"));
    check(!recovery.staleSessions().contains(QStringLiteral("active")),
          QStringLiteral("does not flag the current process as stale"));
    check(recovery.completeSession(QStringLiteral("active"), &errorMessage),
          QStringLiteral("clears a clean session"));

    check(recovery.beginSession(QStringLiteral("crashed"), 2147483000, &errorMessage),
          QStringLiteral("records a candidate unclean session"));
    check(recovery.staleSessions().contains(QStringLiteral("crashed")),
          QStringLiteral("identifies a non-running process marker"));
    check(recovery.dismissStaleSession(QStringLiteral("crashed"), &errorMessage),
          QStringLiteral("can dismiss an acknowledged stale marker"));
    check(!recovery.staleSessions().contains(QStringLiteral("crashed")),
          QStringLiteral("dismissed stale marker no longer recurs"));

    const QString overlay = temporary.filePath(QStringLiteral("overlay.qcow2"));
    QFile overlayFile(overlay);
    check(overlayFile.open(QIODevice::WriteOnly), QStringLiteral("temporary overlay is writable"));
    if (overlayFile.isOpen()) {
        overlayFile.write("not a disk; measurement only");
        overlayFile.close();
    }
    PerformanceMonitor monitor;
    monitor.beginLaunch(QStringLiteral("test-instance"), overlay);
    monitor.attachProcess(QCoreApplication::applicationPid());
    monitor.markQmpReady();
    monitor.markAndroidReady();
    monitor.sample();
    monitor.finish(0);
    const auto measurement = monitor.measurement();
    check(measurement.processId == QCoreApplication::applicationPid(),
          QStringLiteral("associates a measurement with the process"));
    check(measurement.initialOverlayBytes >= 0 && measurement.finalOverlayBytes >= 0,
          QStringLiteral("records overlay size without inspecting guest data"));
    check(monitor.redactedReport().contains(QStringLiteral("local runtime measurement"))
          && !monitor.redactedReport().contains(temporary.path()),
          QStringLiteral("runtime report is local and contains no host path"));
    return failures == 0 ? 0 : 1;
}
