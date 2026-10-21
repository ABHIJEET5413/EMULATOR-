#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>

#include <memory>

class QLockFile;

/** @file launch_channel.h
 * Sends launch/APK requests to the existing UI using a per-user Windows pipe.
 */
class LaunchChannel final : public QObject {
    Q_OBJECT
public:
    enum class Role { Primary, Forwarded, Error };
    /** Create a channel; its lifetime must cover the primary UI event loop. */
    explicit LaunchChannel(QObject* parent = nullptr);
    ~LaunchChannel() override;
    /** Own the UI lease or forward one request to the process that owns it. */
    Role startOrForward(const QString& instancesRoot, const QJsonObject& request);
    /** Return the local pipe name for diagnostics. */
    QString name() const { return m_serverName; }
    /** Return a readable forwarding/listening failure. */
    QString lastError() const { return m_error; }
signals:
    /** A validated launch request arrived from another process. */
    void requestReceived(const QJsonObject& request);
private:
    QLocalServer m_server;
    std::unique_ptr<QLockFile> m_lease;
    QString m_serverName, m_error;
};
