#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>

#include "instance_manager.h"

namespace {
int failures = 0;
void check(bool success, const QString& name)
{
    QTextStream(stdout) << (success ? "PASS " : "FAIL ") << name << '\n';
    if (!success) ++failures;
}
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 5 && arguments.at(1) == "--lock-probe") {
        InstanceManager probe(arguments.at(2), arguments.at(3));
        return probe.acquireInstance(arguments.at(4)) ? 0 : 2;
    }
    const auto root = InstanceManager::findResourceRoot();
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    const QString legacyInstances = temporary.filePath("DroidBox/DroidBox/instances");
    const QString sabiInstances = temporary.filePath("Sabi/Sabi/instances");
    QDir().mkpath(legacyInstances + "/default");
    QFile legacyMarker(legacyInstances + "/default/config.json");
    legacyMarker.open(QIODevice::WriteOnly); legacyMarker.write("{}"); legacyMarker.close();
    QString migrationError;
    check(InstanceManager::migrateLegacyInstances(legacyInstances, sabiInstances, &migrationError),
          "migrate legacy v1 instance root");
    check(!QFileInfo::exists(legacyInstances) && QFileInfo::exists(sabiInstances + "/default/config.json"),
          "legacy migration preserves instance files in the Sabi root");
    check(InstanceManager::migrateLegacyInstances(legacyInstances, sabiInstances, &migrationError),
          "repeat legacy migration is non-destructive");
    const QString resource = temporary.filePath("resources"), instances = temporary.filePath("instances");
    QDir().mkpath(resource + "/vm/configs"); QDir().mkpath(resource + "/vm/qemu"); QDir().mkpath(resource + "/vm/images");
    // Reuse the installed tool; do not copy its dependent DLLs into the test fixture.
    QProcess tool;
    const QString qemuImg = QDir(root).filePath("vm/qemu/qemu-img.exe");
    const QString base = resource + "/vm/images/base.qcow2";
    tool.start(qemuImg, {"create", "-f", "qcow2", base, "16M"});
    if (!tool.waitForFinished(30000) || tool.exitCode() != 0) return 1;
    // Use the real tool directory and an isolated, tiny test base.
    InstanceManager manager(root, instances);
    QJsonObject config{{"baseImage", base}, {"cpuCores", 1}, {"ramMB", 1024},
                       {"display", QJsonObject{{"width", 1280}, {"height", 800}, {"dpi", 160}}}};
    check(manager.createInstance("alpha", config), "create real qcow2 overlay");
    const QString overlay = QDir(manager.getInstanceDir("alpha")).filePath("overlay.qcow2");
    check(QFileInfo::exists(overlay) && QFileInfo(overlay).size() < 1024 * 1024, "new overlay is small");
    check(!manager.createInstance("alpha", config), "reject duplicate name");
    for (const QString& bad : {"../escape", "CON", "two words", "", "a/b", "NUL", "name."})
        check(!manager.isValidName(bad), "reject unsafe name: " + bad);
    InstanceManager second(root, instances);
    check(manager.acquireInstance("alpha"), "acquire running lock");
    check(second.isRunning("alpha"), "other manager observes lock");
    check(!second.acquireInstance("alpha"), "reject duplicate launch");
    QProcess competing;
    competing.start(QCoreApplication::applicationFilePath(), {"--lock-probe", root, instances, "alpha"});
    check(competing.waitForFinished(10000) && competing.exitCode() == 2, "running lock protects against another process");
    check(!second.deleteInstance("alpha") && QFileInfo::exists(overlay), "protect running disk from deletion");
    check(!second.factoryReset("alpha"), "protect running disk from reset");
    check(!second.cloneInstance("alpha", "locked-clone"), "protect running disk from clone");
    auto edit = manager.getConfig("alpha");
    edit["cpuCores"] = InstanceManager::maximumCpuCores();
    check(manager.setConfig("alpha", edit), "save next-start settings while running");
    auto ports = edit.value("network").toObject(); ports["adbHostPort"] = 24000; edit["network"] = ports;
    check(!manager.setConfig("alpha", edit), "reject port change while running");
    manager.releaseInstance("alpha");
    check(!second.isRunning("alpha"), "release running lock");
    competing.start(QCoreApplication::applicationFilePath(), {"--lock-probe", root, instances, "alpha"});
    check(competing.waitForFinished(10000) && competing.exitCode() == 0, "another process can acquire released lock");
    // Write a guest-sector marker, then verify cloning retains it and reset clears it.
    QProcess io;
    const QString qemuIo = QDir(root).filePath("vm/qemu/qemu-io.exe");
    QProcess external;
    external.start(qemuIo, {"-f", "qcow2", overlay});
    const bool opened = external.waitForStarted(5000) && external.waitForReadyRead(5000);
    check(opened, "unmanaged process opens disk");
    if (opened) {
        check(!manager.deleteInstance("alpha") && QFileInfo::exists(overlay) && !manager.getConfig("alpha").isEmpty(), "disk locks protect unmanaged active images and config");
        check(!manager.cloneInstance("alpha", "busy-copy"), "unmanaged writer blocks cloning");
        check(!manager.factoryReset("alpha"), "unmanaged writer blocks reset");
    }
    external.write("quit\n"); external.closeWriteChannel();
    if (!external.waitForFinished(5000)) { external.kill(); external.waitForFinished(); }
    io.start(qemuIo, {"-f", "qcow2", "-c", "write -P 0x5a 0 512", overlay});
    check(io.waitForFinished(30000) && io.exitCode() == 0, "write guest data marker");
    tool.start(qemuImg, {"snapshot", "-c", "before-clone", overlay});
    check(tool.waitForFinished(30000) && tool.exitCode() == 0, "create internal disk snapshot");
    check(manager.cloneInstance("alpha", "beta"), "clone stopped disk");
    const auto alpha = manager.getConfig("alpha"), beta = manager.getConfig("beta");
    check(alpha.value("qmpPort") != beta.value("qmpPort") && alpha.value("adbHostPort") != beta.value("adbHostPort"), "allocate independent ports");
    const QString cloned = QDir(manager.getInstanceDir("beta")).filePath("overlay.qcow2");
    tool.start(qemuImg, {"snapshot", "-l", cloned});
    check(tool.waitForFinished(30000) && tool.exitCode() == 0 && tool.readAllStandardOutput().contains("before-clone"), "clone preserves internal snapshots");
    io.start(qemuIo, {"-f", "qcow2", "-c", "read -P 0x5a 0 512", cloned});
    check(io.waitForFinished(30000) && io.exitCode() == 0, "clone preserves guest data");
    edit = beta; edit["qmpPort"] = alpha.value("adbHostPort");
    check(!manager.setConfig("beta", edit), "reject cross-role port conflict");
    edit = beta; edit["ramMB"] = 1100;
    check(!manager.setConfig("beta", edit), "enforce RAM step");
    edit = beta; edit["cpuCores"] = InstanceManager::maximumCpuCores() + 1;
    check(!manager.setConfig("beta", edit), "enforce host CPU limit");
    edit = beta; edit["camera"] = QJsonObject{{"mode", "test-pattern"}};
    check(!manager.setConfig("beta", edit), "reject unsupported camera device");
    QTcpSocket occupied;
    check(occupied.bind(QHostAddress::LocalHost, 0, QAbstractSocket::DontShareAddress), "reserve external port");
    edit = beta; edit["qmpPort"] = occupied.localPort();
    check(!manager.setConfig("beta", edit), "reject externally occupied port");
    QTcpSocket wildcard;
    check(wildcard.bind(QHostAddress::AnyIPv4, 0,
        QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint), "reserve shared wildcard listener like QEMU hostfwd");
    edit = beta; edit["qmpPort"] = wildcard.localPort();
    check(!manager.setConfig("beta", edit), "reject shared wildcard listener port");
    check(manager.setConfig("beta", beta), "restore original ports after listener regression");
    check(manager.factoryReset("beta"), "factory reset stopped disk");
    io.start(qemuIo, {"-f", "qcow2", "-c", "read -P 0x00 0 512", cloned});
    check(io.waitForFinished(30000) && io.exitCode() == 0, "reset clears guest data");
    check(manager.deleteInstance("beta") && !QFileInfo::exists(cloned), "delete stopped instance");
    // Failed reset must leave the original disk intact.
    check(QFile::rename(base, base + ".hidden"), "hide test base");
    check(!manager.factoryReset("alpha") && QFileInfo::exists(overlay), "failed reset preserves original disk");
    QFile::rename(base + ".hidden", base);
    check(manager.deleteInstance("alpha"), "clean up final instance");
    check(manager.listInstances().isEmpty(), "list reflects deletion");
    QTextStream(stdout) << "Failures: " << failures << '\n';
    return failures ? 1 : 0;
}
