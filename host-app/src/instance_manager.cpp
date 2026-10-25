#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>
#include <QDebug>

#include "instance_manager.h"

#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QJsonObject readObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool portFree(int port)
{
    QTcpSocket socket;
    // On Windows a shared wildcard listener (QEMU hostfwd) can coexist with a
    // loopback-only bind. Reserve all interfaces to detect that listener too.
    if (!socket.bind(QHostAddress::AnyIPv4, static_cast<quint16>(port),
                     QAbstractSocket::DontShareAddress)) return false;
    QTcpSocket ipv6;
    if (ipv6.bind(QHostAddress::AnyIPv6, static_cast<quint16>(port),
                  QAbstractSocket::DontShareAddress)) return true;
    return ipv6.error() == QAbstractSocket::SocketAddressNotAvailableError
        || ipv6.error() == QAbstractSocket::UnsupportedSocketOperationError;
}
}

InstanceManager::InstanceManager(const QString& resourceRoot, const QString& instancesRoot, QObject* parent)
    : QObject(parent), m_resourceRoot(QDir(resourceRoot).absolutePath()),
      m_instancesRoot(QDir(instancesRoot).absolutePath())
{
    QDir().mkpath(m_instancesRoot);
}

QString InstanceManager::findResourceRoot()
{
    const QString exe = QCoreApplication::applicationDirPath();
    // Installed applications must not select an unrelated checkout from the
    // working directory supplied by a shortcut or an APK file association.
    if (QFileInfo::exists(QDir(exe).filePath("installed-layout.json"))) return exe;
    for (const QString& root : {QDir::currentPath(), exe, QDir(exe).filePath("../..")}) {
        if (QFileInfo::exists(QDir(root).filePath("vm/configs/default-instance.json"))
            && QFileInfo::exists(QDir(root).filePath("vm/qemu/qemu-img.exe")))
            return QDir(root).absolutePath();
    }
    return exe;
}

QString InstanceManager::defaultInstancesRoot(const QString& root)
{
    if (QFileInfo::exists(QDir(root).filePath("host-app/CMakeLists.txt")))
        return QDir(root).filePath("instances");
    const QString sabiRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                 .filePath("instances");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        QString migrationError;
        const QString legacyRoot = QDir(localAppData).filePath("DroidBox/DroidBox/instances");
        if (!migrateLegacyInstances(legacyRoot, sabiRoot, &migrationError))
            qWarning().noquote() << "Could not migrate existing DroidBox instances:" << migrationError;
    }
    return sabiRoot;
}

bool InstanceManager::migrateLegacyInstances(const QString& legacyInstancesRoot,
                                             const QString& sabiInstancesRoot,
                                             QString* errorMessage)
{
    const QFileInfo legacy(QDir::cleanPath(legacyInstancesRoot));
    const QFileInfo destination(QDir::cleanPath(sabiInstancesRoot));
    if (!legacy.exists() || destination.exists()) return true;
    if (legacy.isSymLink() || destination.isSymLink()) {
        if (errorMessage) *errorMessage = "Migration refused a symbolic link or junction in an instance path.";
        return false;
    }
    const QDir destinationParent = destination.dir();
    if (!QDir().mkpath(destinationParent.absolutePath())) {
        if (errorMessage) *errorMessage = "Could not create the Sabi data directory.";
        return false;
    }
    if (!QDir().rename(legacy.absoluteFilePath(), destination.absoluteFilePath())) {
        if (errorMessage) *errorMessage = "Could not move existing instances into the Sabi data directory. The original data was left unchanged.";
        return false;
    }
    QFile marker(QDir(destination.absoluteFilePath()).filePath(".migrated-from-droidbox-v1"));
    if (marker.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
        marker.write("Migrated by Sabi without altering qcow2 overlays or backing files.\n");
    qInfo() << "Migrated DroidBox instance root to Sabi:" << destination.absoluteFilePath();
    return true;
}

int InstanceManager::maximumCpuCores() { return std::clamp(QThread::idealThreadCount(), 1, 8); }
int InstanceManager::maximumRamMB()
{
    quint64 bytes = 0;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) bytes = status.ullTotalPhys;
#endif
    if (!bytes) return 1024; // Conservative if host memory cannot be queried.
    return std::min(8192, static_cast<int>(bytes / (2 * 1024 * 1024 * 512)) * 512);
}

bool InstanceManager::fail(const QString& message) const
{
    m_error = message;
    qWarning().noquote() << message;
    return false;
}

bool InstanceManager::safeName(const QString& name) const
{
    static const QRegularExpression syntax("^[A-Za-z0-9][A-Za-z0-9_-]{0,47}$");
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$",
                                            QRegularExpression::CaseInsensitiveOption);
    return syntax.match(name).hasMatch() && !reserved.match(name).hasMatch();
}

QString InstanceManager::getInstanceDir(const QString& name) const
{
    if (!safeName(name)) return {};
    QFileInfo root(m_instancesRoot);
    QFileInfo child(QDir(m_instancesRoot).filePath(name));
    // Reject junctions/symlinks, including a replaced instances root.
    if (root.isSymLink() || child.isSymLink()) return {};
    if (child.exists() && child.canonicalPath().compare(root.canonicalFilePath(), Qt::CaseInsensitive) != 0)
        return {};
    return child.absoluteFilePath();
}

bool InstanceManager::isValidName(const QString& name) const
{
    return !getInstanceDir(name).isEmpty() && !QFileInfo::exists(getInstanceDir(name));
}

std::unique_ptr<QLockFile> InstanceManager::registryLock() const
{
    auto lock = std::make_unique<QLockFile>(QDir(m_instancesRoot).filePath(".registry.lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) { fail("Instance storage is busy in another operation. Try again when it finishes."); return {}; }
    return lock;
}

std::unique_ptr<QLockFile> InstanceManager::diskLock(const QString& name) const
{
    const QString dir = getInstanceDir(name);
    if (dir.isEmpty() || !QFileInfo::exists(QDir(dir).filePath("config.json"))) {
        fail("The instance does not exist or its directory is unsafe."); return {};
    }
    auto lock = std::make_unique<QLockFile>(QDir(m_instancesRoot).filePath(name.toLower() + ".running.lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) { fail("Stop the instance before changing its disk or launching it again."); return {}; }
    return lock;
}

QJsonObject InstanceManager::getConfig(const QString& name) const
{
    const QString dir = getInstanceDir(name);
    auto config = dir.isEmpty() ? QJsonObject{} : readObject(QDir(dir).filePath("config.json"));
    if (!config.isEmpty() && QFileInfo::exists(QDir(m_resourceRoot).filePath("installed-layout.json")))
        config["qemuPath"] = QDir(m_resourceRoot).filePath("vm/qemu/qemu-system-x86_64.exe");
    return config;
}

std::shared_ptr<void> InstanceManager::diskFileLock(const QString& name, bool deleting) const
{
#ifdef Q_OS_WIN
    const QString path = QDir::toNativeSeparators(QDir(getInstanceDir(name)).filePath("overlay.qcow2"));
    // Deny write sharing for the whole operation. qemu-img check opens read-only
    // and is NOT proof that another QEMU/qemu-io process is not writing the disk.
    // Allow our own read/copy and rename/delete operations while holding this handle.
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | (deleting ? DELETE : 0),
                                FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
        return std::shared_ptr<void>(handle, [](void* value) { CloseHandle(value); });
    fail("Cannot lock the disk. Stop every QEMU or disk-tool process using this instance and check file permissions.");
#else
    Q_UNUSED(name)
    Q_UNUSED(deleting)
    fail("Exclusive disk operations require this project's Windows host build.");
#endif
    return {};
}

QList<InstanceManager::InstanceInfo> InstanceManager::listInstances() const
{
    QList<InstanceInfo> result;
    for (const QString& name : QDir(m_instancesRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto config = getConfig(name);
        if (config.isEmpty()) continue;
        const auto display = config.value("display").toObject();
        result.append({name, getInstanceDir(name), QString("%1x%2").arg(display.value("width").toInt()).arg(display.value("height").toInt()),
                       isRunning(name), config.value("cpuCores").toInt(), config.value("ramMB").toInt(),
                       QFileInfo(QDir(getInstanceDir(name)).filePath("overlay.qcow2")).size()});
    }
    return result;
}

bool InstanceManager::isRunning(const QString& name) const
{
    if (m_runningLocks.contains(name.toLower())) return true;
    if (getConfig(name).isEmpty()) return false;
    QLockFile lock(QDir(m_instancesRoot).filePath(name.toLower() + ".running.lock"));
    lock.setStaleLockTime(0);
    return !lock.tryLock(0);
}

QPair<int, int> InstanceManager::allocatePorts() const
{
    QSet<int> used;
    for (const auto& info : listInstances()) {
        const auto config = getConfig(info.name);
        used.insert(config.value("qmpPort").toInt()); used.insert(config.value("adbHostPort").toInt());
    }
    auto next = [&used](int start) {
        while (start <= 65535 && (used.contains(start) || !portFree(start))) ++start;
        if (start > 65535) return 0;
        used.insert(start); return start;
    };
    const int qmp = next(4444);
    return {qmp, next(5555)};
}

QJsonObject InstanceManager::normalized(const QString& name, QJsonObject config) const
{
    config["name"] = name;
    config["overlayImage"] = QDir(getInstanceDir(name)).filePath("overlay.qcow2");
    for (const QString& key : {QString("qemuPath"), QString("baseImage")}) {
        const QString value = config.value(key).toString();
        if (!value.isEmpty() && QDir::isRelativePath(value)) config[key] = QDir(m_resourceRoot).absoluteFilePath(value);
    }
    auto network = config.value("network").toObject();
    if (!config.contains("adbHostPort")) config["adbHostPort"] = network.value("adbHostPort");
    network["adbHostPort"] = config.value("adbHostPort"); config["network"] = network;
    return config;
}

bool InstanceManager::validate(const QString& name, const QJsonObject& config, bool probePorts) const
{
    const int cpu = config.value("cpuCores").toInt(), ram = config.value("ramMB").toInt();
    if (cpu < 1 || cpu > maximumCpuCores()) return fail(QString("CPU cores must be between 1 and %1.").arg(maximumCpuCores()));
    if (ram < 1024 || ram > maximumRamMB() || ram % 512) return fail(QString("RAM must be 1024-%1 MB in steps of 512 MB.").arg(maximumRamMB()));
    const auto display = config.value("display").toObject();
    const int width = display.value("width").toInt(), height = display.value("height").toInt();
    const int dpi = display.value("dpi").toInt();
    if (width < 320 || height < 320 || width > 4096 || height > 4096 || dpi < 80 || dpi > 640)
        return fail("Display size must be 320-4096 pixels per side, with DPI 80-640.");
    const int qmp = config.value("qmpPort").toInt(), adb = config.value("adbHostPort").toInt();
    const auto network = config.value("network").toObject();
    if (network.contains("adbHostPort") && network.value("adbHostPort").toInt() != adb)
        return fail("adbHostPort and network.adbHostPort disagree. Save matching values before starting the instance.");
    if (qmp < 1024 || adb < 1024 || qmp > 65535 || adb > 65535 || qmp == adb) return fail("QMP and ADB need distinct ports between 1024 and 65535.");
    for (const auto& info : listInstances()) {
        if (info.name.compare(name, Qt::CaseInsensitive) == 0) continue;
        const auto other = getConfig(info.name);
        for (int port : {other.value("qmpPort").toInt(), other.value("adbHostPort").toInt()})
            if (port == qmp || port == adb) return fail(QString("Port %1 is already assigned to %2.").arg(port).arg(info.name));
    }
    if (probePorts && (!portFree(qmp) || !portFree(adb))) return fail("A configured QMP or ADB port is occupied by another process. Stop it or select different ports.");
    const auto camera = config.value("camera").toObject();
    const QString mode = camera.value("mode").toString("none");
    if (mode != "none" && mode != "webcam") return fail("This QEMU build has no test-pattern camera device. Select None or experimental USB webcam.");
    if (mode == "webcam") {
        const QRegularExpression hex("^0x[0-9a-fA-F]{4}$");
        if (!hex.match(camera.value("vendorId").toString()).hasMatch() || !hex.match(camera.value("productId").toString()).hasMatch())
            return fail("Webcam vendor and product IDs must use four hexadecimal digits, for example 0x046d.");
    }
    if (!QFileInfo::exists(config.value("baseImage").toString())) return fail("The base image is missing. Provision a Bliss base image before creating or resetting an instance.");
    return true;
}

bool InstanceManager::writeConfig(const QString& path, const QJsonObject& config)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(config).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail("Could not save instance settings: " + file.errorString());
    return true;
}

bool InstanceManager::runImageTool(const QStringList& arguments)
{
    QProcess process;
    process.start(QDir(m_resourceRoot).filePath("vm/qemu/qemu-img.exe"), arguments);
    if (!process.waitForStarted(5000)) return fail("Cannot start qemu-img. Check the bundled QEMU installation.");
    if (!process.waitForFinished(120000)) { process.kill(); process.waitForFinished(); return fail("The disk operation timed out; the original disk has been preserved."); }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail("Disk operation failed: " + QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
    return true;
}

bool InstanceManager::createOverlay(const QString& path, const QString& base)
{
    // Relative backing names allow instances and the base to move together.
    return runImageTool({"create", "-f", "qcow2", "-b", QDir(QFileInfo(path).absolutePath()).relativeFilePath(base), "-F", "qcow2", path});
}

bool InstanceManager::initialize()
{
    if (!listInstances().isEmpty()) return true;
    if (QFileInfo::exists(QDir(m_resourceRoot).filePath("installed-layout.json"))
        && preparedBaseImage().isEmpty()) return false;
    const QString dir = getInstanceDir("default");
    if (dir.isEmpty()) return fail("The default instance directory is unsafe. Check for a junction or symlink.");
    if (QFileInfo::exists(QDir(dir).filePath("overlay.qcow2"))) {
        auto lock = registryLock(); if (!lock) return false;
        auto config = normalized("default", readObject(QDir(m_resourceRoot).filePath("vm/configs/default-instance.json")));
        const QString prepared = preparedBaseImage();
        if (QFileInfo::exists(prepared)) config["baseImage"] = prepared;
        if (!validate("default", config, false)) return false;
        return writeConfig(QDir(dir).filePath("config.json"), config);
    }
    return createInstance("default");
}

QString InstanceManager::preparedBaseImage() const
{
    const QString source = QDir(m_resourceRoot).filePath("vm/images/bliss-provisioned.qcow2");
    const QString layout = QDir(m_resourceRoot).filePath("installed-layout.json");
    if (!QFileInfo::exists(layout)) return source;
    const auto metadata = readObject(layout);
    const QString digest = metadata.value("baseImageSha256").toString().toLower();
    if (!QRegularExpression("^[0-9a-f]{64}$").match(digest).hasMatch()) {
        fail("The installed Android image manifest is invalid. Reinstall Sabi."); return {};
    }
    const QString dataRoot = QFileInfo(m_instancesRoot).absolutePath();
    const QString bases = QDir(dataRoot).filePath("bases");
    const QString version = QDir(bases).filePath(digest);
    if (QFileInfo(dataRoot).isSymLink() || QFileInfo(bases).isSymLink()
        || QFileInfo(version).isSymLink() || !QDir().mkpath(version)) {
        fail("Cannot prepare the Android base directory. Check permissions and junctions."); return {};
    }
    QLockFile lock(QDir(bases).filePath(".preparation.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { fail("Another Sabi process is preparing the Android image. Wait for it to finish."); return {}; }
    const QString target = QDir(version).filePath("bliss-provisioned.qcow2");
    if (QFileInfo(target).isSymLink()) { fail("The cached Android base must not be a link."); return {}; }
    if (QFileInfo::exists(target)) {
        if (QFileInfo(target).size() == metadata.value("baseImageBytes").toVariant().toLongLong()) return target;
        fail("The preserved Android base has an unexpected size. Preserve your instance data and reinstall Sabi."); return {};
    }
    QFile input(source);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        fail("Cannot copy the Android base to user data. Check free disk space and permissions."); return {};
    }
    qInfo() << "Preparing the installed Android base in user data:" << target;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        const QByteArray block = input.read(4 * 1024 * 1024);
        if (block.isEmpty() && input.error() != QFileDevice::NoError) {
            fail("Could not read the installed Android image."); return {};
        }
        hash.addData(block);
        if (output.write(block) != block.size()) {
            fail("Could not copy the Android image. Free disk space and retry."); return {};
        }
    }
    if (QString::fromLatin1(hash.result().toHex()) != digest || !output.commit()) {
        fail("Android image verification or publication failed. Reinstall Sabi and check disk space."); return {};
    }
    // Old versions are retained: every overlay keeps its original backing base.
    return target;
}

bool InstanceManager::createInstance(const QString& name, const QJsonObject& overrides)
{
    auto lock = registryLock(); if (!lock) return false;
    if (!isValidName(name)) return fail("Choose a unique name with letters, digits, hyphens, or underscores (maximum 48 characters).");
    auto config = readObject(QDir(m_resourceRoot).filePath("vm/configs/default-instance.json"));
    if (QFileInfo::exists(QDir(m_resourceRoot).filePath("installed-layout.json")))
        config["ramMB"] = std::clamp(config.value("ramMB").toInt(4096), 1024, maximumRamMB());
    for (auto it = overrides.begin(); it != overrides.end(); ++it) config[it.key()] = it.value();
    // This repository's original image predates the ADB startup repair. Fresh
    // instances must use the separately prepared base, never silently regress.
    const QString prepared = preparedBaseImage();
    if (!overrides.contains("baseImage") && QFileInfo::exists(prepared)) config["baseImage"] = prepared;
    else if (!overrides.contains("baseImage"))
        return fail("Prepare vm/images/bliss-provisioned.qcow2 with image-build/prepare-instance-base.sh first. See docs/PHASE-5-TESTING.md. The original base lacks the Phase 4 ADB startup repair.");
    const auto ports = allocatePorts();
    config["qmpPort"] = ports.first; config["adbHostPort"] = ports.second;
    auto network = config.value("network").toObject(); network["adbHostPort"] = ports.second; config["network"] = network;
    config = normalized(name, config);
    if (!validate(name, config, true)) return false;
    const QString dir = getInstanceDir(name);
    if (!QDir().mkpath(dir)) return fail("Cannot create the instance directory. Check free space and permissions.");
    if (createOverlay(QDir(dir).filePath("overlay.qcow2"), config.value("baseImage").toString())
        && writeConfig(QDir(dir).filePath("config.json"), config)) return true;
    // Only this call's newly-created directory is removed on failure.
    QDir(dir).removeRecursively(); return false;
}

bool InstanceManager::cloneInstance(const QString& sourceName, const QString& newName)
{
    auto lock = registryLock(); if (!lock) return false;
    auto disk = diskLock(sourceName); if (!disk) return false;
    auto fileLock = diskFileLock(sourceName); if (!fileLock) return false;
    if (!isValidName(newName)) return fail("The clone needs a valid, unique instance name.");
    auto config = getConfig(sourceName);
    const auto ports = allocatePorts(); config["qmpPort"] = ports.first; config["adbHostPort"] = ports.second;
    auto network = config.value("network").toObject(); network["adbHostPort"] = ports.second; config["network"] = network;
    config = normalized(newName, config);
    if (!validate(newName, config, true)) return false;
    const QString dir = getInstanceDir(newName);
    if (!QDir().mkpath(dir)) return fail("Cannot create the clone directory.");
    // Preserve internal disk snapshots as well as the current guest data.
    const QString source = QDir(getInstanceDir(sourceName)).filePath("overlay.qcow2");
    QProcess info;
    info.start(QDir(m_resourceRoot).filePath("vm/qemu/qemu-img.exe"), {"info", "--output=json", source});
    if (!info.waitForFinished(30000) || info.exitCode() != 0) {
        info.kill(); info.waitForFinished(); QDir(dir).removeRecursively();
        return fail("Could not read the source disk's backing chain. Stop its VM and check the image.");
    }
    const auto metadata = QJsonDocument::fromJson(info.readAllStandardOutput()).object();
    const QString backing = metadata.value("full-backing-filename").toString();
    const QString target = QDir(dir).filePath("overlay.qcow2");
    if (!runImageTool({"check", source})) { QDir(dir).removeRecursively(); return false; }
    if (!QFile::copy(source, target)) {
        QDir(dir).removeRecursively(); return fail("Cannot copy the stopped instance disk. Check free disk space and permissions.");
    }
    bool chainReady = true;
    if (!backing.isEmpty()) {
        const QString relativeBacking = QDir(dir).relativeFilePath(backing);
        chainReady = runImageTool({"rebase", "-u", "-b", relativeBacking, "-F", "qcow2", target});
    }
    if (chainReady && writeConfig(QDir(dir).filePath("config.json"), config)) return true;
    QDir(dir).removeRecursively(); return false;
}

bool InstanceManager::deleteInstance(const QString& name)
{
    auto lock = registryLock(); if (!lock) return false;
    auto disk = diskLock(name); if (!disk) return false;
    auto fileLock = diskFileLock(name, true); if (!fileLock) return false;
    const QString dir = getInstanceDir(name);
    const auto config = getConfig(name);
    if (dir.isEmpty() || !QDir(dir).removeRecursively()) {
        if (QFileInfo::exists(QDir(dir).filePath("overlay.qcow2")))
            writeConfig(QDir(dir).filePath("config.json"), config);
        return fail("Could not remove all instance files. Check file permissions and close tools using its directory.");
    }
    return true;
}

bool InstanceManager::factoryReset(const QString& name)
{
    auto lock = registryLock(); if (!lock) return false;
    auto disk = diskLock(name); if (!disk) return false;
    auto fileLock = diskFileLock(name, true); if (!fileLock) return false;
    auto config = getConfig(name);
    const QString dir = getInstanceDir(name), overlay = QDir(dir).filePath("overlay.qcow2");
    const QString fresh = overlay + ".new", backup = overlay + ".reset-backup";
    if (QFileInfo::exists(fresh) || QFileInfo::exists(backup)) return fail("A previous reset left recovery files. Preserve and inspect them before retrying.");
    if (!validate(name, config, false)) return false;
    if (!createOverlay(fresh, config.value("baseImage").toString())) { QFile::remove(fresh); return false; }
    if (!QFile::rename(overlay, backup)) { QFile::remove(fresh); return fail("Cannot preserve the original disk for reset."); }
    if (!QFile::rename(fresh, overlay)) {
        QFile::rename(backup, overlay); QFile::remove(fresh); return fail("Cannot publish the fresh disk; the original was restored.");
    }
    if (!QFile::remove(backup)) qWarning() << "Reset completed, but recovery disk remains:" << backup;
    return true;
}

bool InstanceManager::setConfig(const QString& name, const QJsonObject& input)
{
    auto lock = registryLock(); if (!lock) return false;
    const auto previous = getConfig(name);
    if (previous.isEmpty()) return fail("The instance configuration is missing or invalid.");
    auto consistent = input;
    const auto requestedNetwork = input.value("network").toObject();
    const bool topChanged = input.value("adbHostPort") != previous.value("adbHostPort");
    const bool nestedChanged = requestedNetwork.value("adbHostPort") != previous.value("network").toObject().value("adbHostPort");
    if (topChanged && nestedChanged && input.value("adbHostPort") != requestedNetwork.value("adbHostPort"))
        return fail("The two ADB port settings disagree. Choose one port for this instance.");
    if (!topChanged && nestedChanged && requestedNetwork.contains("adbHostPort")) consistent["adbHostPort"] = requestedNetwork.value("adbHostPort");
    auto config = normalized(name, consistent);
    // Settings cannot redirect a managed disk or silently swap its reset base.
    config["baseImage"] = previous.value("baseImage"); config["qemuPath"] = previous.value("qemuPath");
    const bool running = isRunning(name);
    if (running && (config.value("qmpPort") != previous.value("qmpPort") || config.value("adbHostPort") != previous.value("adbHostPort")))
        return fail("Stop the instance before changing its ports.");
    if (!validate(name, config, !running)) return false;
    return writeConfig(QDir(getInstanceDir(name)).filePath("config.json"), config);
}

bool InstanceManager::acquireInstance(const QString& name)
{
    auto registry = registryLock(); if (!registry) return false;
    auto lock = diskLock(name); if (!lock) return false;
    if (!validate(name, getConfig(name), true)) return false;
    m_runningLocks.insert(name.toLower(), std::shared_ptr<QLockFile>(std::move(lock)));
    return true;
}

void InstanceManager::releaseInstance(const QString& name) { m_runningLocks.remove(name.toLower()); }
