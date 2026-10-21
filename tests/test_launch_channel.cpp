#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include "launch_channel.h"

#include <functional>

namespace {
int failures = 0;
void check(bool success, const QString& name)
{
    QTextStream(stdout) << (success ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!success) ++failures;
}
bool waitUntil(const std::function<bool()>& condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(); QThread::msleep(5);
    }
    return condition();
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 4 && arguments.at(1) == "--send") {
        QFile file(arguments.at(3)); if (!file.open(QIODevice::ReadOnly)) return 3;
        LaunchChannel client;
        return client.startOrForward(arguments.at(2), QJsonDocument::fromJson(file.readAll()).object())
            == LaunchChannel::Role::Forwarded ? 0 : 2;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString root = temporary.filePath("instances with spaces");
    const QJsonObject request{{"command", "launch"}, {"instance", "gaming"},
        {"apk", QString::fromUtf8("C:/Apps/\xc3\xa9 example \"quoted\".apk")}, {"showInstances", false}};
    QString pipe;
    {
        LaunchChannel primary;
        check(primary.startOrForward(root, request) == LaunchChannel::Role::Primary, "first UI owns the lease");
        pipe = primary.name();
        QList<QJsonObject> received;
        QObject::connect(&primary, &LaunchChannel::requestReceived, &application,
            [&](const QJsonObject& incoming) { received.append(incoming); });
        const QString payload = temporary.filePath("request.json");
        QFile file(payload);
        check(file.open(QIODevice::WriteOnly), "open request fixture");
        file.write(QJsonDocument(request).toJson()); file.close();
        QProcess client;
        client.start(QCoreApplication::applicationFilePath(), {"--send", root, payload});
        check(client.waitForStarted(5000), "start independent launch process");
        check(waitUntil([&] { return client.state() == QProcess::NotRunning; }), "forwarding process exits promptly");
        check(client.exitCode() == 0 && received.size() == 1 && received.first() == request,
              "forward instance and exact Unicode/spaces/quoted APK path across processes");

        QLocalSocket invalid;
        invalid.connectToServer(pipe);
        check(invalid.waitForConnected(1000), "connect malformed-message fixture");
        invalid.write("{bad json}\n"); invalid.flush();
        check(waitUntil([&] { return invalid.state() == QLocalSocket::UnconnectedState; }) && received.size() == 1,
              "malformed requests are rejected without dispatch");
        QLocalSocket oversized;
        oversized.connectToServer(pipe);
        check(oversized.waitForConnected(1000), "connect oversized-message fixture");
        oversized.write(QByteArray(70 * 1024, 'x')); oversized.flush();
        check(waitUntil([&] { return oversized.state() == QLocalSocket::UnconnectedState; }) && received.size() == 1,
              "oversized requests are bounded and rejected");
        LaunchChannel independent;
        check(independent.startOrForward(temporary.filePath("other instances"), request) == LaunchChannel::Role::Primary
              && independent.name() != pipe, "independent data roots have separate channels");
        LaunchChannel bad;
        check(bad.startOrForward(temporary.filePath("bad"), {}) == LaunchChannel::Role::Error,
              "invalid outbound requests fail clearly");
    }
    LaunchChannel replacement;
    check(replacement.startOrForward(root, request) == LaunchChannel::Role::Primary,
          "closing the UI releases the lease and pipe");
    const QString busyRoot = temporary.filePath("busy");
    QDir().mkpath(busyRoot);
    QLockFile lease(QDir(busyRoot).filePath(".ui.lock"));
    check(lease.tryLock(0), "reserve a UI lease without a listening pipe");
    LaunchChannel busy;
    check(busy.startOrForward(busyRoot, request) == LaunchChannel::Role::Error
          && busy.lastError().contains("owns"), "unresponsive owner fails clearly without starting a second UI");
    QTextStream(stdout) << "Failures: " << failures << Qt::endl;
    return failures ? 1 : 0;
}
