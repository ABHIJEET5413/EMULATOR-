#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>

#include "launch_channel.h"

namespace {
constexpr int maximumMessageBytes = 64 * 1024;
bool validRequest(const QJsonObject& request)
{
    return request.value("command").toString() == "launch"
        && request.value("instance").isString() && request.value("apk").isString()
        && request.value("showInstances").isBool();
}
}

LaunchChannel::LaunchChannel(QObject* parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = m_server.nextPendingConnection()) {
            socket->setReadBufferSize(maximumMessageBytes + 1);
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QTimer::singleShot(5000, socket, &QLocalSocket::disconnectFromServer);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer] {
                buffer->append(socket->readAll());
                if (buffer->size() > maximumMessageBytes) { socket->abort(); return; }
                const auto newline = buffer->indexOf('\n');
                if (newline < 0) return;
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(buffer->left(newline), &error);
                if (error.error != QJsonParseError::NoError || !document.isObject()
                    || !validRequest(document.object())) { socket->abort(); return; }
                socket->write("OK\n");
                socket->flush();
                socket->disconnectFromServer();
                emit requestReceived(document.object());
            });
        }
    });
}

LaunchChannel::~LaunchChannel()
{
    m_server.close();
    m_lease.reset();
}

LaunchChannel::Role LaunchChannel::startOrForward(const QString& instancesRoot, const QJsonObject& request)
{
    if (!validRequest(request)) { m_error = "The launch request is invalid."; return Role::Error; }
    const QByteArray message = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (message.size() > maximumMessageBytes) { m_error = "The launch request is too large."; return Role::Error; }
    if (!QDir().mkpath(instancesRoot)) { m_error = "Cannot create the instance directory."; return Role::Error; }
    const QString root = QFileInfo(instancesRoot).canonicalFilePath();
    if (root.isEmpty() || QFileInfo(instancesRoot).isSymLink()) {
        m_error = "The instance directory must be an accessible directory, not a link."; return Role::Error;
    }
    m_serverName = "DroidBox-" + QString::fromLatin1(QCryptographicHash::hash(
        root.toCaseFolded().toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
    m_lease = std::make_unique<QLockFile>(QDir(root).filePath(".ui.lock"));
    m_lease->setStaleLockTime(0);
    // Windows permits multiple named-pipe listeners with the same name, so
    // listening alone is not an ownership test. Hold a process lease as well.
    if (m_lease->tryLock(0)) {
        if (m_server.listen(m_serverName)) return Role::Primary;
        m_error = "Cannot open the DroidBox launch channel: " + m_server.errorString();
        m_lease.reset(); return Role::Error;
    }
    QLocalSocket socket;
    socket.connectToServer(m_serverName);
    if (!socket.waitForConnected(2000)) {
        m_error = "Another DroidBox process owns these instances but is not accepting requests. Wait for it to finish starting or close it normally.";
        return Role::Error;
    }
    if (socket.write(message) != message.size()
        || (socket.bytesToWrite() && !socket.waitForBytesWritten(2000))) {
        m_error = "Could not send the launch request to DroidBox."; return Role::Error;
    }
    QElapsedTimer timer; timer.start();
    QByteArray reply;
    while (!reply.contains('\n') && timer.elapsed() < 5000) {
        reply.append(socket.readAll());
        if (reply.contains('\n')) break;
        if (!socket.waitForReadyRead(static_cast<int>(5000 - timer.elapsed()))) {
            reply.append(socket.readAll()); break;
        }
    }
    if (reply != "OK\n") {
        m_error = "The running DroidBox did not acknowledge the request. It may still be starting.";
        return Role::Error;
    }
    return Role::Forwarded;
}
