#pragma once

#include <QJsonObject>
#include <QString>

/**
 * @file diagnostics_service.h
 * @brief Creates explicitly requested, redacted local Sabi support reports.
 */
class DiagnosticsService final {
public:
    /** Return the folder containing local Sabi log files. */
    static QString logDirectory();
    /** Build a local-only summary that excludes secrets and user-specific paths. */
    static QString redactedSummary(const QJsonObject& config);
    /** Save a redacted text report at a path selected by the user. */
    static bool saveRedactedReport(const QString& path, const QJsonObject& config,
                                   QString* errorMessage = nullptr);
    /** Open the local log folder in the user's file manager. */
    static bool openLogDirectory(QString* errorMessage = nullptr);
};
