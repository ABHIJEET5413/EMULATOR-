#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QLockFile>
#include <QObject>
#include <QString>

#include <memory>

/** @file instance_manager.h
 * Persistent per-instance configuration, qcow2 disks, and process locks.
 */
class InstanceManager final : public QObject {
    Q_OBJECT
public:
    struct InstanceInfo {
        QString name, path, resolution;
        bool isRunning{false};
        int cpuCores{1}, ramMB{1024};
        qint64 overlaySize{0};
    };
    /** Construct with explicit resource and writable instance roots. */
    explicit InstanceManager(const QString& resourceRoot, const QString& instancesRoot,
                             QObject* parent = nullptr);
    /** Import the existing default disk without replacing its contents. */
    bool initialize();
    /** Discover the development or installed resource directory. */
    static QString findResourceRoot();
    /** Return the writable instance directory for a resource installation. */
    static QString defaultInstancesRoot(const QString& resourceRoot);
    /** Return the host limits enforced by settings and launch. */
    static int maximumCpuCores();
    static int maximumRamMB();
    /** Enumerate saved instances and their current lock status. */
    QList<InstanceInfo> listInstances() const;
    /** Create a fresh qcow2 overlay and atomic configuration. */
    bool createInstance(const QString& name, const QJsonObject& config = {});
    /** Copy a stopped instance's disk and allocate independent ports. */
    bool cloneInstance(const QString& sourceName, const QString& newName);
    /** Remove a stopped instance. UI callers must obtain confirmation. */
    bool deleteInstance(const QString& name);
    /** Replace a stopped disk with a new overlay, preserving settings. */
    bool factoryReset(const QString& name);
    /** Read or atomically save a validated configuration. */
    QJsonObject getConfig(const QString& name) const;
    bool setConfig(const QString& name, const QJsonObject& config);
    /** Return the validated absolute instance directory or an empty string. */
    QString getInstanceDir(const QString& name) const;
    /** Check a portable filename and reject duplicates. */
    bool isValidName(const QString& name) const;
    /** Allocate unused ports across both QMP and ADB roles. */
    QPair<int, int> allocatePorts() const;
    /** Check or hold a cross-process lock for the lifetime of a VM. */
    bool isRunning(const QString& name) const;
    bool acquireInstance(const QString& name);
    void releaseInstance(const QString& name);
    /** Return the last human-readable operation failure. */
    QString lastError() const { return m_error; }
    /** Return roots for independent worker-thread manager objects. */
    QString resourceRoot() const { return m_resourceRoot; }
    QString instancesRoot() const { return m_instancesRoot; }
private:
    bool fail(const QString& message) const;
    bool safeName(const QString& name) const;
    QJsonObject normalized(const QString& name, QJsonObject config) const;
    bool validate(const QString& name, const QJsonObject& config, bool probePorts) const;
    bool writeConfig(const QString& path, const QJsonObject& config);
    bool createOverlay(const QString& path, const QString& base);
    bool runImageTool(const QStringList& arguments);
    QString preparedBaseImage() const;
    std::unique_ptr<QLockFile> registryLock() const;
    std::unique_ptr<QLockFile> diskLock(const QString& name) const;
    std::shared_ptr<void> diskFileLock(const QString& name, bool deleting = false) const;
    QString m_resourceRoot, m_instancesRoot;
    mutable QString m_error;
    QHash<QString, std::shared_ptr<QLockFile>> m_runningLocks;
};
