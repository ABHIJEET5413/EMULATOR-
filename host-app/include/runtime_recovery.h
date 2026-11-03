#pragma once

#include <QString>
#include <QStringList>

/**
 * @file runtime_recovery.h
 * @brief Records short-lived VM session markers and identifies unclean exits.
 *
 * Markers contain only an instance name, process identifier, and start time.
 * They stay on the local machine and never inspect or modify an Android disk.
 */
class RuntimeRecovery final {
public:
    /** Create a recovery service storing markers below @p markerRoot. */
    explicit RuntimeRecovery(QString markerRoot);

    /** Return the marker directory used by this service. */
    QString markerRoot() const;
    /** Start (or replace) the marker for a QEMU process. */
    bool beginSession(const QString& instanceName, qint64 processId,
                      QString* errorMessage = nullptr) const;
    /** Remove a marker after an expected, clean stop. */
    bool completeSession(const QString& instanceName,
                         QString* errorMessage = nullptr) const;
    /** Return instance names with markers whose QEMU process is no longer alive. */
    QStringList staleSessions() const;
    /** Remove a stale marker only after the user has been informed. */
    bool dismissStaleSession(const QString& instanceName,
                             QString* errorMessage = nullptr) const;

private:
    QString markerPath(const QString& instanceName) const;
    static QString safeMarkerName(const QString& instanceName);
    static bool processIsAlive(qint64 processId);

    QString m_markerRoot;
};
