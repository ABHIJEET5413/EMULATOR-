#include <algorithm>

#include "instance_workspace_model.h"

QList<InstanceManager::InstanceInfo> InstanceWorkspaceModel::filterAndSort(
    QList<InstanceManager::InstanceInfo> instances, const QString& query, const int statusFilter, const Sort sort)
{
    instances.erase(std::remove_if(instances.begin(), instances.end(), [&](const auto& instance) {
        const bool statusMatch = statusFilter == 0 || (statusFilter == 1 && instance.isRunning)
            || (statusFilter == 2 && !instance.isRunning);
        return !statusMatch || (!query.trimmed().isEmpty() && !instance.name.contains(query, Qt::CaseInsensitive)
            && !instance.labels.join(' ').contains(query, Qt::CaseInsensitive));
    }), instances.end());
    std::sort(instances.begin(), instances.end(), [sort](const auto& left, const auto& right) {
        if (sort == Sort::Status && left.isRunning != right.isRunning) return left.isRunning > right.isRunning;
        if (sort == Sort::DiskSize && left.overlaySize != right.overlaySize) return left.overlaySize > right.overlaySize;
        return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
    });
    return instances;
}

QString InstanceWorkspaceModel::healthSummary(const InstanceManager::InstanceInfo& instance)
{
    if (!instance.backingImageAvailable) return QStringLiteral("Base image missing — preserve the overlay and reinstall/repair Sabi.");
    if (instance.availableBytes >= 0 && instance.availableBytes < 1024LL * 1024LL * 1024LL)
        return QStringLiteral("Low host disk space — free at least 1 GB before installing apps or cloning.");
    return instance.isRunning ? QStringLiteral("Running") : QStringLiteral("Ready");
}
