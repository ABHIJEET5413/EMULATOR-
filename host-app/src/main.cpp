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
    static QFile logFile(QDir(logDirectory).filePath(QStringLiteral("droidbox-%1.log")
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
    QCoreApplication::setApplicationName(QStringLiteral("DroidBox"));
    QCoreApplication::setOrganizationName(QStringLiteral("DroidBox"));
    qInstallMessageHandler(messageHandler);

    qInfo() << "DroidBox desktop interface initialized.";
    QString apkToInstall;
    const QStringList arguments = application->arguments();
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        if (QFileInfo(arguments.at(index)).suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) == 0) {
            apkToInstall = arguments.at(index);
            break;
        }
    }

    const QString root = InstanceManager::findResourceRoot();
    InstanceManager manager(root, InstanceManager::defaultInstancesRoot(root));
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
    const auto instances = manager.listInstances();
    const int selected = arguments.indexOf("--instance");
    if (selected >= 0 && selected + 1 < arguments.size())
        window.launchInstance(arguments.at(selected + 1), apkToInstall);
    else if (!apkToInstall.isEmpty()) window.launchInstance("default", apkToInstall);
    else if (instances.size() == 1 && !arguments.contains("--instances")) window.launchInstance(instances.first().name);
    else window.show();
    // A failed launch must leave an accessible launcher, never an invisible process.
    if (QApplication::topLevelWidgets().size() == 1) window.show();
    return application->exec();
}
