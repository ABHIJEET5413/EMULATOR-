#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QUrl>

#include "diagnostics_service.h"

namespace {

QString safeFileName(const QString& path)
{
    return path.isEmpty() ? QStringLiteral("not configured") : QFileInfo(path).fileName();
}

QString redactText(QString value)
{
    static const QRegularExpression userPath(
        QStringLiteral("[A-Za-z]:[\\\\/]+Users[\\\\/]+[^\\\\/]+"),
        QRegularExpression::CaseInsensitiveOption);
    return value.replace(userPath, QStringLiteral("<user-profile>"));
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) *errorMessage = message;
}

} // namespace

QString DiagnosticsService::logDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString DiagnosticsService::redactedSummary(const QJsonObject& config)
{
    const QJsonObject display = config.value(QStringLiteral("display")).toObject();
    const QJsonObject camera = config.value(QStringLiteral("camera")).toObject();
    return QStringLiteral(
               "Sabi local support report\n"
               "Generated: %1\n"
               "Operating system: %2\n"
               "Instance: %3\n"
               "CPU cores: %4\n"
               "RAM MB: %5\n"
               "Display: %6 (%7x%8 at %9 DPI)\n"
               "QMP port: %10\n"
               "ADB port: %11\n"
               "Camera mode: %12\n"
               "Base image file: %13\n"
               "Overlay file: %14\n"
               "Log folder: %15\n"
               "\nPrivacy: This report contains no ADB keys, APK contents, guest files,"
               " complete host paths, or log contents. Logs are not copied automatically.\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
             redactText(QSysInfo::prettyProductName()),
             redactText(config.value(QStringLiteral("name")).toString()),
             QString::number(config.value(QStringLiteral("cpuCores")).toInt()),
             QString::number(config.value(QStringLiteral("ramMB")).toInt()),
             display.value(QStringLiteral("profile")).toString(QStringLiteral("custom")),
             QString::number(display.value(QStringLiteral("width")).toInt()),
             QString::number(display.value(QStringLiteral("height")).toInt()),
             QString::number(display.value(QStringLiteral("dpi")).toInt()),
             QString::number(config.value(QStringLiteral("qmpPort")).toInt()),
             QString::number(config.value(QStringLiteral("adbHostPort")).toInt()),
             camera.value(QStringLiteral("mode")).toString(QStringLiteral("none")),
             safeFileName(config.value(QStringLiteral("baseImage")).toString()),
             safeFileName(config.value(QStringLiteral("overlayImage")).toString()),
             QStringLiteral("<Sabi local data>"));
}

bool DiagnosticsService::saveRedactedReport(const QString& path, const QJsonObject& config,
                                            QString* errorMessage)
{
    QFile report(path);
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        setError(errorMessage, QStringLiteral("Could not write the support report: %1").arg(report.errorString()));
        return false;
    }
    QTextStream stream(&report);
    stream << redactedSummary(config);
    return true;
}

bool DiagnosticsService::openLogDirectory(QString* errorMessage)
{
    const QString directory = logDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        setError(errorMessage, QStringLiteral("Sabi could not create its local log folder."));
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
        setError(errorMessage, QStringLiteral("Windows could not open Sabi's local log folder."));
        return false;
    }
    return true;
}
