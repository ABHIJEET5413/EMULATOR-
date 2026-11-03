#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "runtime_recovery.h"

#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

} // namespace

RuntimeRecovery::RuntimeRecovery(QString markerRoot)
    : m_markerRoot(QDir::cleanPath(std::move(markerRoot)))
{
}

QString RuntimeRecovery::markerRoot() const
{
    return m_markerRoot;
}

QString RuntimeRecovery::safeMarkerName(const QString& instanceName)
{
    QString safe;
    for (const QChar character : instanceName) {
        safe += (character.isLetterOrNumber() || character == '-' || character == '_')
            ? character : QChar('_');
    }
    return safe;
}

QString RuntimeRecovery::markerPath(const QString& instanceName) const
{
    return QDir(m_markerRoot).filePath(safeMarkerName(instanceName) + QStringLiteral(".json"));
}

bool RuntimeRecovery::beginSession(const QString& instanceName, const qint64 processId,
                                   QString* errorMessage) const
{
    if (safeMarkerName(instanceName).isEmpty() || processId <= 0) {
        setError(errorMessage, QStringLiteral("Sabi could not record VM recovery information because the instance or process is invalid."));
        return false;
    }
    if (!QDir().mkpath(m_markerRoot)) {
        setError(errorMessage, QStringLiteral("Sabi could not create its local recovery folder."));
        return false;
    }

    QSaveFile marker(markerPath(instanceName));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(errorMessage, QStringLiteral("Sabi could not write its local recovery marker: %1").arg(marker.errorString()));
        return false;
    }
    const QJsonObject document{{QStringLiteral("schemaVersion"), 1},
                               {QStringLiteral("instance"), instanceName},
                               {QStringLiteral("pid"), static_cast<double>(processId)},
                               {QStringLiteral("startedUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    marker.write(QJsonDocument(document).toJson(QJsonDocument::Compact));
    if (!marker.commit()) {
        setError(errorMessage, QStringLiteral("Sabi could not finalize its local recovery marker: %1").arg(marker.errorString()));
        return false;
    }
    qInfo().noquote() << "Recorded VM recovery marker for" << instanceName;
    return true;
}

bool RuntimeRecovery::completeSession(const QString& instanceName, QString* errorMessage) const
{
    const QString path = markerPath(instanceName);
    if (!QFileInfo::exists(path) || QFile::remove(path)) {
        return true;
    }
    setError(errorMessage, QStringLiteral("Sabi could not clear its recovery marker: %1").arg(path));
    return false;
}

bool RuntimeRecovery::processIsAlive(const qint64 processId)
{
    if (processId <= 0) {
        return false;
    }
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (!process) {
        return false;
    }
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return false;
#endif
}

QStringList RuntimeRecovery::staleSessions() const
{
    QStringList stale;
    const QDir directory(m_markerRoot);
    for (const QFileInfo& fileInfo : directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile marker(fileInfo.absoluteFilePath());
        if (!marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QJsonObject object = QJsonDocument::fromJson(marker.readAll()).object();
        const QString name = object.value(QStringLiteral("instance")).toString();
        const qint64 processId = object.value(QStringLiteral("pid")).toVariant().toLongLong();
        if (name.isEmpty() || !processIsAlive(processId)) {
            stale.append(name.isEmpty() ? fileInfo.completeBaseName() : name);
        }
    }
    stale.removeDuplicates();
    return stale;
}

bool RuntimeRecovery::dismissStaleSession(const QString& instanceName, QString* errorMessage) const
{
    return completeSession(instanceName, errorMessage);
}
