#pragma once

#include <QDateTime>
#include <QString>

/**
 * @file performance_monitor.h
 * @brief Collects small, local, user-exportable measurements for one VM launch.
 */
class PerformanceMonitor final {
public:
    struct Measurement {
        QString instanceName;
        qint64 processId{0};
        QDateTime launchStartedUtc;
        QDateTime qmpReadyUtc;
        QDateTime androidReadyUtc;
        QDateTime stoppedUtc;
        qint64 peakWorkingSetBytes{-1};
        qint64 initialOverlayBytes{-1};
        qint64 finalOverlayBytes{-1};
        double latestCpuPercent{-1.0};
        int exitCode{0};
    };

    /** Begin a new launch measurement; no process is sampled until attachProcess(). */
    void beginLaunch(const QString& instanceName, const QString& overlayPath);
    /** Attach the launched QEMU process and capture its initial disk size. */
    void attachProcess(qint64 processId);
    /** Mark the QMP handshake milestone. */
    void markQmpReady();
    /** Mark Android's ADB boot-completed milestone. */
    void markAndroidReady();
    /** Sample local CPU and working-set data while the process is running. */
    void sample();
    /** Finalize the measurement after QEMU exits. */
    void finish(int exitCode);
    /** Return the latest in-memory measurement. */
    Measurement measurement() const;
    /** Return a redacted, local-only text report. */
    QString redactedReport() const;
    /** Save the report only at a path chosen by the user. */
    bool saveRedactedReport(const QString& path, QString* errorMessage = nullptr) const;

private:
    qint64 elapsedMs(const QDateTime& milestone) const;

    Measurement m_measurement;
    QString m_overlayPath;
    qint64 m_lastProcessCpuMs{-1};
    QDateTime m_lastSampleUtc;
};
