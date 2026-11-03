#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "performance_monitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

#ifdef Q_OS_WIN
qint64 fileTimeToMilliseconds(const FILETIME& value)
{
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return static_cast<qint64>(ticks.QuadPart / 10000ULL);
}
#endif

} // namespace

void PerformanceMonitor::beginLaunch(const QString& instanceName, const QString& overlayPath)
{
    m_measurement = {};
    m_measurement.instanceName = instanceName;
    m_measurement.launchStartedUtc = QDateTime::currentDateTimeUtc();
    m_overlayPath = overlayPath;
    m_lastProcessCpuMs = -1;
    m_lastSampleUtc = {};
}

void PerformanceMonitor::attachProcess(const qint64 processId)
{
    m_measurement.processId = processId;
    m_measurement.initialOverlayBytes = QFileInfo(m_overlayPath).exists()
        ? QFileInfo(m_overlayPath).size() : -1;
    sample();
}

void PerformanceMonitor::markQmpReady()
{
    if (!m_measurement.launchStartedUtc.isNull() && m_measurement.qmpReadyUtc.isNull()) {
        m_measurement.qmpReadyUtc = QDateTime::currentDateTimeUtc();
    }
}

void PerformanceMonitor::markAndroidReady()
{
    if (!m_measurement.launchStartedUtc.isNull() && m_measurement.androidReadyUtc.isNull()) {
        m_measurement.androidReadyUtc = QDateTime::currentDateTimeUtc();
    }
}

void PerformanceMonitor::sample()
{
#ifdef Q_OS_WIN
    if (m_measurement.processId <= 0) {
        return;
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                 static_cast<DWORD>(m_measurement.processId));
    if (!process) {
        return;
    }
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(process, &memory, sizeof(memory))) {
        m_measurement.peakWorkingSetBytes = qMax(m_measurement.peakWorkingSetBytes,
            static_cast<qint64>(memory.PeakWorkingSetSize));
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        const qint64 cpuMs = fileTimeToMilliseconds(kernel) + fileTimeToMilliseconds(user);
        if (m_lastProcessCpuMs >= 0 && m_lastSampleUtc.isValid()) {
            const qint64 intervalMs = m_lastSampleUtc.msecsTo(now);
            if (intervalMs > 0) {
                m_measurement.latestCpuPercent = (100.0 * (cpuMs - m_lastProcessCpuMs)) / intervalMs;
            }
        }
        m_lastProcessCpuMs = cpuMs;
        m_lastSampleUtc = now;
    }
    CloseHandle(process);
#endif
}

void PerformanceMonitor::finish(const int exitCode)
{
    sample();
    m_measurement.exitCode = exitCode;
    m_measurement.stoppedUtc = QDateTime::currentDateTimeUtc();
    m_measurement.finalOverlayBytes = QFileInfo(m_overlayPath).exists()
        ? QFileInfo(m_overlayPath).size() : -1;
}

PerformanceMonitor::Measurement PerformanceMonitor::measurement() const
{
    return m_measurement;
}

qint64 PerformanceMonitor::elapsedMs(const QDateTime& milestone) const
{
    return m_measurement.launchStartedUtc.isValid() && milestone.isValid()
        ? m_measurement.launchStartedUtc.msecsTo(milestone) : -1;
}

QString PerformanceMonitor::redactedReport() const
{
    const auto bytesText = [](const qint64 bytes) {
        return bytes < 0 ? QStringLiteral("not sampled")
            : QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
    };
    const auto durationText = [this](const QDateTime& milestone) {
        const qint64 milliseconds = elapsedMs(milestone);
        return milliseconds < 0 ? QStringLiteral("not reached")
            : QString::number(static_cast<double>(milliseconds) / 1000.0, 'f', 2) + QStringLiteral(" s");
    };
    return QStringLiteral(
        "Sabi local runtime measurement\n"
        "Instance: %1\n"
        "Launch to QMP ready: %2\n"
        "Launch to Android ready: %3\n"
        "Launch to stop: %4\n"
        "Peak QEMU working set: %5\n"
        "Latest sampled QEMU CPU: %6\n"
        "Overlay at process start: %7\n"
        "Overlay at stop: %8\n"
        "QEMU exit code: %9\n"
        "\nScope: local point-in-time measurements only. CPU is one process sample, not a benchmark; "
        "no guest files, host paths, logs, keys, APKs, or telemetry are included.\n")
        .arg(m_measurement.instanceName.isEmpty() ? QStringLiteral("unknown") : m_measurement.instanceName,
             durationText(m_measurement.qmpReadyUtc), durationText(m_measurement.androidReadyUtc),
             durationText(m_measurement.stoppedUtc), bytesText(m_measurement.peakWorkingSetBytes),
             m_measurement.latestCpuPercent < 0.0 ? QStringLiteral("not sampled")
                 : QString::number(m_measurement.latestCpuPercent, 'f', 1) + QStringLiteral(" %"),
             bytesText(m_measurement.initialOverlayBytes), bytesText(m_measurement.finalOverlayBytes),
             QString::number(m_measurement.exitCode));
}

bool PerformanceMonitor::saveRedactedReport(const QString& path, QString* errorMessage) const
{
    QFile report(path);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        setError(errorMessage, QStringLiteral("Could not write the local runtime report: %1").arg(report.errorString()));
        return false;
    }
    QTextStream stream(&report);
    stream << redactedReport();
    return true;
}
