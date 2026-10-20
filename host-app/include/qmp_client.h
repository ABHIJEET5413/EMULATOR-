#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

/**
 * @file qmp_client.h
 * @brief Asynchronous newline-delimited JSON client for QEMU's QMP protocol.
 */
class QmpClient final : public QObject {
    Q_OBJECT

public:
    /** Create a QMP client. */
    explicit QmpClient(QObject* parent = nullptr);

    /** Begin connecting and retry every 500 ms until disconnectedFromHost(). */
    void connectToHost(const QString& host, quint16 port);

    /** Disconnect and disable automatic reconnects. */
    void disconnectFromHost();

    /** Send a QMP command and return its request ID, or an empty string on failure. */
    QString execute(const QString& command, const QJsonObject& arguments = {});

    /** Return true after the QMP greeting and capability handshake complete. */
    bool isConnected() const;

signals:
    /** Emitted after qmp_capabilities succeeds. */
    void connected();
    /** Emitted when an established QMP connection is lost or closed. */
    void disconnected();
    /** Emitted for a successful command response. */
    void responseReceived(const QString& id, const QJsonObject& result);
    /** Emitted when QMP returns an error for a command. */
    void commandFailed(const QString& id, const QString& message);
    /** Emitted for QMP asynchronous events such as STOP and SHUTDOWN. */
    void eventReceived(const QString& event, const QJsonObject& data);
    /** Emitted for transport, handshake, and malformed protocol errors. */
    void errorOccurred(const QString& message);

private slots:
    void handleSocketConnected();
    void handleSocketDisconnected();
    void handleSocketError(QAbstractSocket::SocketError error);
    void handleReadyRead();
    void retryConnection();

private:
    void processLine(const QByteArray& line);
    bool writeMessage(const QJsonObject& message);
    void scheduleReconnect();

    QTcpSocket m_socket;
    QTimer m_reconnectTimer;
    QByteArray m_receiveBuffer;
    QString m_host;
    quint16 m_port{0};
    quint64 m_nextCommandId{1};
    bool m_shouldReconnect{false};
    bool m_waitingForGreeting{false};
    bool m_handshakePending{false};
    bool m_sessionConnected{false};
};
