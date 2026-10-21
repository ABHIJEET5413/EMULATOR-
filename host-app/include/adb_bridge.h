#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

/**
 * @file adb_bridge.h
 * @brief Runs asynchronous ADB commands and tracks the Android guest state.
 */
class AdbBridge final : public QObject {
    Q_OBJECT

public:
    /** Create a bridge; ADB is located beside the app or on PATH by default. */
    explicit AdbBridge(QObject* parent = nullptr);
    ~AdbBridge() override;

    /** Use a specific adb executable, or clear the override to search standard locations. */
    void setAdbPath(const QString& path);

    /** Return the configured or discovered adb executable path. */
    QString adbPath() const;

    /** Start the ADB server and asynchronously connect to the guest. */
    void connectToGuest(const QString& host, int port);

    /** Stop connection polling after the VM stops. */
    void disconnectFromGuest();

    /** Poll sys.boot_completed until Android is ready or the timeout expires. */
    void waitForBoot(int timeoutMs = 120000);

    /** Install or replace an APK on the ready guest. */
    void installApk(const QString& apkPath);

    /** Push a local file into a guest directory and request a media scan. */
    void pushFile(const QString& localPath, const QString& remoteDirectory);

    /** Ask Android's media scanner to index a guest file path. */
    void mediaScan(const QString& remotePath);

    /** Capture the guest screen as PNG data and save it to a local path. */
    void screenshot(const QString& savePath);

    /** Send an Android key event such as HOME=3 or BACK=4. */
    void sendKeyEvent(int keycode);

    /** Run a shell command on the connected guest. */
    void shellCommand(const QString& command);

    /** Return true after ADB reports the guest device connected. */
    bool isConnected() const;

    /** Return true after Android reports sys.boot_completed=1. */
    bool isBootCompleted() const;

    /** Return the serial for the connected guest, such as localhost:5555. */
    QString getDeviceSerial() const;

signals:
    /** Emitted after ADB connects to the guest device. */
    void connected(const QString& serial);
    /** Emitted after the guest disconnects or the VM stops. */
    void disconnected();
    /** Emitted when Android reports sys.boot_completed=1. */
    void bootCompleted();
    /** Emitted when an APK installation finishes. */
    void apkInstallFinished(bool success, const QString& message);
    /** Emitted when a file push finishes. */
    void filePushFinished(bool success, const QString& message);
    /** Emitted after a screenshot is saved. */
    void screenshotSaved(const QString& path);
    /** Emitted for command IDs, exit codes, and captured text output. */
    void commandFinished(quint64 id,
                         int exitCode,
                         const QString& standardOutput,
                         const QString& standardError);
    /** Emitted while APK installation is active or after it finishes. */
    void installProgress(const QString& status);
    /** Emitted for human-readable ADB and guest errors. */
    void errorOccurred(const QString& message);

private:
    struct AdbResult {
        int exitCode{-1};
        QByteArray standardOutput;
        QByteArray standardError;
        bool timedOut{false};
    };

    using Completion = std::function<void(const AdbResult&)>;

    QString resolveAdbPath() const;
    quint64 runAdbAsync(const QStringList& arguments,
                        int timeoutMs,
                        Completion completion = {},
                        bool useGuestSerial = true);
    void startAdbServer(quint64 generation);
    void probeGuestListener(quint64 generation);
    void tryConnect(quint64 generation);
    void verifyTransport(quint64 generation);
    void pollBootStatus(quint64 generation);
    void pollConnectionHealth(quint64 generation);
    void handleDisconnected(quint64 generation);
    bool requireBootCompleted(const QString& operation);
    QString resultMessage(const AdbResult& result) const;

    QString m_adbPath;
    QString m_serial;
    QString m_host;
    int m_port{0};
    int m_connectAttempts{0};
    int m_bootTimeoutMs{120000};
    quint64 m_nextCommandId{0};
    quint64 m_connectionGeneration{0};
    bool m_adbPathExplicit{false};
    bool m_connected{false};
    bool m_bootCompleted{false};
    bool m_connectCommandRunning{false};
    bool m_bootCommandRunning{false};
    bool m_healthCommandRunning{false};
    QElapsedTimer m_bootElapsed;
    QTimer m_connectTimer;
    QTimer m_bootPollTimer;
    QTimer m_healthTimer;
};
