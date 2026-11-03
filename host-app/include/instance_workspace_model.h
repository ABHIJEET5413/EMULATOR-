#pragma once

#include <QList>
#include <QString>

#include "instance_manager.h"

/** @file instance_workspace_model.h
 * @brief Pure search, filtering, sorting, and health presentation logic for the instance workspace.
 */
class InstanceWorkspaceModel final {
public:
    enum class Sort { Name, Status, DiskSize };
    /** Return rows matching a case-insensitive name/label query and running-state filter. */
    static QList<InstanceManager::InstanceInfo> filterAndSort(QList<InstanceManager::InstanceInfo> instances,
                                                               const QString& query, int statusFilter, Sort sort);
    /** Return a concise, user-facing health state for one instance. */
    static QString healthSummary(const InstanceManager::InstanceInfo& instance);
};
