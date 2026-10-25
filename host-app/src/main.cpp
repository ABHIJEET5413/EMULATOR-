#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

#include "main_window.h"
#include "instance_manager.h"
#include "instance_list_window.h"
#include "launch_channel.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

QMutex g_logMutex;

QString messageTypeName(const QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO ");
    case QtWarningMsg:
        return QStringLiteral("WARN ");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKWN");
}

void messageHandler(const QtMsgType type,
                    const QMessageLogContext& context,
                    const QString& message)
{
    Q_UNUSED(context)

    QMutexLocker lock(&g_logMutex);
    QString logDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (logDirectory.isEmpty()) {
        logDirectory = QCoreApplication::applicationDirPath();
    }
    if (!QDir().mkpath(logDirectory)) {
        logDirectory = QCoreApplication::applicationDirPath();
    }
    // Independent application processes cannot safely share a persistent file
    // offset on Windows. Keep each process's complete log in its own file.
    static QFile logFile(QDir(logDirectory).filePath(QStringLiteral("sabi-%1.log")
        .arg(QCoreApplication::applicationPid())));

    const QString line = QStringLiteral("[%1] [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                                  messageTypeName(type),
                                  message);

    if (logFile.isOpen()
        || logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&logFile);
        stream << line;
        stream.flush();
    }

    const QByteArray localLine = line.toLocal8Bit();
    std::fwrite(localLine.constData(), 1, static_cast<size_t>(localLine.size()), stderr);
    std::fflush(stderr);

    if (type == QtFatalMsg) {
        std::abort();
    }
}

} // namespace

int main(int argc, char* argv[])
{
    bool initializeOnly = false;
    for (int index = 1; index < argc; ++index)
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--initialize-only")) initializeOnly = true;
    std::unique_ptr<QCoreApplication> application;
    if (initializeOnly) application = std::make_unique<QCoreApplication>(argc, argv);
    else application = std::make_unique<QApplication>(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Sabi"));
    QCoreApplication::setOrganizationName(QStringLiteral("Sabi"));
    qInstallMessageHandler(messageHandler);

    qInfo() << "Sabi desktop interface initialized.";
    QString apkToInstall;
    const QStringList arguments = application->arguments();
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        if (QFileInfo(arguments.at(index)).suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) == 0) {
            apkToInstall = QFileInfo(arguments.at(index)).absoluteFilePath();
            break;
        }
    }

    const QString root = InstanceManager::findResourceRoot();
    InstanceManager manager(root, InstanceManager::defaultInstancesRoot(root));
    const int selected = arguments.indexOf("--instance");
    const QString requestedInstance = selected >= 0 && selected + 1 < arguments.size()
        ? arguments.at(selected + 1) : QString{};
    const QJsonObject request{{"command", "launch"}, {"instance", requestedInstance},
        {"apk", apkToInstall}, {"showInstances", arguments.contains("--instances")}};
    LaunchChannel channel;
    if (!initializeOnly) {
        const auto role = channel.startOrForward(manager.instancesRoot(), request);
        if (role == LaunchChannel::Role::Forwarded) return 0;
        if (role == LaunchChannel::Role::Error) {
            QMessageBox::warning(nullptr, "Sabi launch unavailable", channel.lastError()); return 1;
        }
    }
    if (!manager.initialize()) {
        if (initializeOnly) { qCritical().noquote() << manager.lastError(); return 1; }
        QMessageBox::critical(nullptr, "Instance storage unavailable", manager.lastError()); return 1;
    }
    if ((initializeOnly || !apkToInstall.isEmpty()) && manager.getConfig("default").isEmpty()
        && !manager.createInstance("default")) {
        if (initializeOnly) qCritical().noquote() << manager.lastError();
        else QMessageBox::critical(nullptr, "Default instance unavailable", manager.lastError());
        return 1;
    }
    if (initializeOnly) { qInfo() << "Default instance is initialized."; return 0; }
    InstanceListWindow window(&manager);
    const auto launch = [&window, &manager](const QJsonObject& incoming) {
        const auto instances = manager.listInstances();
        const QString instance = incoming.value("instance").toString();
        const QString apk = incoming.value("apk").toString();
        if (!instance.isEmpty()) window.launchInstance(instance, apk);
        else if (!apk.isEmpty()) {
            if (manager.getConfig("default").isEmpty() && !manager.createInstance("default")) {
                QMessageBox::warning(&window, "Default instance unavailable", manager.lastError());
                window.show(); return;
            }
            window.launchInstance("default", apk);
        } else if (instances.size() == 1 && !incoming.value("showInstances").toBool())
            window.launchInstance(instances.first().name);
        else { window.showNormal(); window.raise(); window.activateWindow(); }
        // A failed launch must leave an accessible launcher.
        if (QApplication::topLevelWidgets().size() == 1) window.show();
    };
    QObject::connect(&channel, &LaunchChannel::requestReceived, &window, launch);
    launch(request);
    return application->exec();
}
