#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QAbstractSocket>

#include "qmp_client.h"

namespace {

QString qmpErrorText(const QJsonObject& errorObject)
{
    const QString description = errorObject.value(QStringLiteral("desc")).toString();
    const QString errorClass = errorObject.value(QStringLiteral("class")).toString();
    if (!description.isEmpty() && !errorClass.isEmpty()) {
        return QStringLiteral("%1 (%2)").arg(description, errorClass);
    }
    if (!description.isEmpty()) {
        return description;
    }
    return QStringLiteral("QEMU returned an unspecified QMP error.");
}

} // namespace

QmpClient::QmpClient(QObject* parent)
    : QObject(parent)
{
    m_reconnectTimer.setInterval(500);
    m_reconnectTimer.setSingleShot(true);

    connect(&m_socket, &QTcpSocket::connected,
            this, &QmpClient::handleSocketConnected);
    connect(&m_socket, &QTcpSocket::disconnected,
            this, &QmpClient::handleSocketDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead,
            this, &QmpClient::handleReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred,
            this, &QmpClient::handleSocketError);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &QmpClient::retryConnection);
}

void QmpClient::connectToHost(const QString& host, quint16 port)
{
    const QString requestedHost = host.trimmed();
    if (requestedHost.isEmpty() || port == 0) {
        emit errorOccurred(QStringLiteral("QMP host and port must be valid."));
        return;
    }

    if (m_shouldReconnect && m_host == requestedHost && m_port == port
        && m_socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    disconnectFromHost();
    m_host = requestedHost;
    m_port = port;
    m_shouldReconnect = true;
    m_receiveBuffer.clear();
    m_socket.connectToHost(m_host, m_port);
}

void QmpClient::disconnectFromHost()
{
    m_shouldReconnect = false;
    m_reconnectTimer.stop();

    const bool wasConnected = m_sessionConnected;
    m_sessionConnected = false;
    m_waitingForGreeting = false;
    m_handshakePending = false;
    m_receiveBuffer.clear();
    m_socket.abort();

    if (wasConnected) {
        emit disconnected();
    }
}

QString QmpClient::execute(const QString& command, const QJsonObject& arguments)
{
    if (!isConnected()) {
        emit errorOccurred(QStringLiteral("Cannot send QMP command '%1': QMP is not connected.")
                               .arg(command));
        return {};
    }

    const QString trimmedCommand = command.trimmed();
    if (trimmedCommand.isEmpty()) {
        emit errorOccurred(QStringLiteral("Cannot send an empty QMP command."));
        return {};
    }

    const QString id = QString::number(m_nextCommandId++);
    QJsonObject request{
        {QStringLiteral("execute"), trimmedCommand},
        {QStringLiteral("id"), id}
    };
    if (!arguments.isEmpty()) {
        request.insert(QStringLiteral("arguments"), arguments);
    }

    if (!writeMessage(request)) {
        return {};
    }
    return id;
}

bool QmpClient::isConnected() const
{
    return m_sessionConnected
        && m_socket.state() == QAbstractSocket::ConnectedState;
}

void QmpClient::handleSocketConnected()
{
    if (!m_shouldReconnect) {
        m_socket.abort();
        return;
    }

    m_reconnectTimer.stop();
    m_receiveBuffer.clear();
    m_waitingForGreeting = true;
    m_handshakePending = false;
}

void QmpClient::handleSocketDisconnected()
{
    const bool wasConnected = m_sessionConnected;
    m_sessionConnected = false;
    m_waitingForGreeting = false;
    m_handshakePending = false;
    m_receiveBuffer.clear();

    if (wasConnected) {
        emit disconnected();
    }
    scheduleReconnect();
}

void QmpClient::handleSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)

    if (m_shouldReconnect) {
        emit errorOccurred(QStringLiteral("QMP socket error: %1").arg(m_socket.errorString()));
        scheduleReconnect();
    }
}

void QmpClient::handleReadyRead()
{
    m_receiveBuffer.append(m_socket.readAll());

    qsizetype newlinePosition = -1;
    while ((newlinePosition = m_receiveBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_receiveBuffer.left(newlinePosition);
        m_receiveBuffer.remove(0, newlinePosition + 1);
        line = line.trimmed();
        if (!line.isEmpty()) {
            processLine(line);
        }
    }
}

void QmpClient::retryConnection()
{
    if (!m_shouldReconnect || m_host.isEmpty() || m_port == 0
        || m_socket.state() != QAbstractSocket::UnconnectedState) {
        return;
    }

    m_socket.connectToHost(m_host, m_port);
}

void QmpClient::processLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit errorOccurred(QStringLiteral("Received invalid JSON from QMP: %1")
                               .arg(parseError.errorString()));
        return;
    }

    const QJsonObject message = document.object();
    if (message.contains(QStringLiteral("QMP"))) {
        if (!m_waitingForGreeting) {
            emit errorOccurred(QStringLiteral("Received an unexpected QMP greeting."));
            return;
        }

        m_waitingForGreeting = false;
        m_handshakePending = true;
        if (!writeMessage({{QStringLiteral("execute"), QStringLiteral("qmp_capabilities")}})) {
            m_handshakePending = false;
        }
        return;
    }

    if (message.contains(QStringLiteral("event"))) {
        const QString event = message.value(QStringLiteral("event")).toString();
        const QJsonObject data = message.value(QStringLiteral("data")).toObject();
        if (!event.isEmpty()) {
            emit eventReceived(event, data);
        }
        return;
    }

    if (m_handshakePending) {
        m_handshakePending = false;
        if (message.contains(QStringLiteral("error"))) {
            emit errorOccurred(QStringLiteral("QMP capability handshake failed: %1")
                                   .arg(qmpErrorText(message.value(QStringLiteral("error")).toObject())));
            m_socket.abort();
            scheduleReconnect();
            return;
        }
        if (message.contains(QStringLiteral("return"))) {
            m_sessionConnected = true;
            m_reconnectTimer.stop();
            emit connected();
            return;
        }

        emit errorOccurred(QStringLiteral("QMP returned an unexpected capability response."));
        m_socket.abort();
        scheduleReconnect();
        return;
    }

    const QJsonValue idValue = message.value(QStringLiteral("id"));
    const QString id = idValue.isString() ? idValue.toString()
                                          : idValue.toVariant().toString();
    if (id.isEmpty()) {
        emit errorOccurred(QStringLiteral("Received a QMP response without a command ID."));
        return;
    }

    if (message.contains(QStringLiteral("error"))) {
        emit commandFailed(id, qmpErrorText(message.value(QStringLiteral("error")).toObject()));
        return;
    }

    if (!message.contains(QStringLiteral("return"))) {
        emit errorOccurred(QStringLiteral("Received a QMP response without return or error data."));
        return;
    }

    const QJsonValue returnValue = message.value(QStringLiteral("return"));
    const QJsonObject result = returnValue.isObject()
        ? returnValue.toObject()
        : QJsonObject{{QStringLiteral("value"), returnValue}};
    emit responseReceived(id, result);
}

bool QmpClient::writeMessage(const QJsonObject& message)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        emit errorOccurred(QStringLiteral("Cannot write to QMP: the socket is not connected."));
        return false;
    }

    QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (m_socket.write(payload) < 0) {
        emit errorOccurred(QStringLiteral("Could not write to QMP: %1").arg(m_socket.errorString()));
        return false;
    }
    return true;
}

void QmpClient::scheduleReconnect()
{
    if (m_shouldReconnect && m_socket.state() == QAbstractSocket::UnconnectedState
        && !m_reconnectTimer.isActive()) {
        m_reconnectTimer.start();
    }
}
