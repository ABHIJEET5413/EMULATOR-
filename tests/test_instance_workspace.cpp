#include <QCoreApplication>
#include <QTextStream>

#include "instance_workspace_model.h"

namespace {
int failures = 0;
void check(bool condition, const QString& name)
{
    QTextStream(stdout) << (condition ? "PASS " : "FAIL ") << name << Qt::endl;
    if (!condition) ++failures;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    InstanceManager::InstanceInfo alpha; alpha.name = "alpha"; alpha.labels = {"games", "personal"}; alpha.overlaySize = 20; alpha.backingImageAvailable = true; alpha.availableBytes = 5LL * 1024 * 1024 * 1024;
    InstanceManager::InstanceInfo beta; beta.name = "beta"; beta.labels = {"work"}; beta.isRunning = true; beta.overlaySize = 10; beta.backingImageAvailable = true; beta.availableBytes = 5LL * 1024 * 1024 * 1024;
    InstanceManager::InstanceInfo broken; broken.name = "broken"; broken.backingImageAvailable = false;
    const QList<InstanceManager::InstanceInfo> fixtures{alpha, beta, broken};
    const auto labelSearch = InstanceWorkspaceModel::filterAndSort(fixtures, "games", 0, InstanceWorkspaceModel::Sort::Name);
    check(labelSearch.size() == 1 && labelSearch.first().name == "alpha", "workspace search includes labels");
    const auto running = InstanceWorkspaceModel::filterAndSort(fixtures, {}, 1, InstanceWorkspaceModel::Sort::Name);
    check(running.size() == 1 && running.first().name == "beta", "workspace status filter isolates running instances");
    const auto diskOrder = InstanceWorkspaceModel::filterAndSort(fixtures, {}, 0, InstanceWorkspaceModel::Sort::DiskSize);
    check(diskOrder.first().name == "alpha", "workspace disk sort is deterministic");
    check(InstanceWorkspaceModel::healthSummary(broken).contains("Base image missing"), "workspace reports missing backing image safely");
    alpha.availableBytes = 512LL * 1024 * 1024;
    check(InstanceWorkspaceModel::healthSummary(alpha).contains("Low host disk space"), "workspace warns about low host storage");
    return failures == 0 ? 0 : 1;
}
