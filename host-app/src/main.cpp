#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTextStream>

#include "main_window.h"

#include <cstdio>
#include <cstdlib>

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
    static QFile logFile(QDir(logDirectory).filePath(QStringLiteral("droidbox.log")));

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
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DroidBox"));
    QCoreApplication::setOrganizationName(QStringLiteral("DroidBox"));
    qInstallMessageHandler(messageHandler);

    qInfo() << "DroidBox desktop interface initialized.";
    QString apkToInstall;
    const QStringList arguments = application.arguments();
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        if (QFileInfo(arguments.at(index)).suffix().compare(QStringLiteral("apk"), Qt::CaseInsensitive) == 0) {
            apkToInstall = arguments.at(index);
            break;
        }
    }

    MainWindow window;
    window.show();
    if (!apkToInstall.isEmpty()) {
        window.queueApkInstall(apkToInstall);
    }
    return application.exec();
}
