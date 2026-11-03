#pragma once
#include <QHash>
#include <QPointer>
#include <QWidget>
#include <functional>

class InstanceManager;
class MainWindow;
class QTableWidget;
class QThread;
class QLineEdit;
class QComboBox;
class RuntimeRecovery;
/** @file instance_list_window.h
 * Desktop launcher with asynchronous instance disk operations.
 */
class InstanceListWindow final : public QWidget {
    Q_OBJECT
public:
    /** Create a launcher; manager must outlive this window. */
    explicit InstanceListWindow(InstanceManager* manager, RuntimeRecovery* recovery = nullptr,
                                QWidget* parent = nullptr);
    /** Join disk work and disconnect child callbacks before members are destroyed. */
    ~InstanceListWindow() override;
    /** Launch or focus a selected instance, optionally queueing an APK. */
    void launchInstance(const QString& name, const QString& apk = {});
protected:
    /** Keep an active disk worker alive until it completes. */
    void closeEvent(QCloseEvent* event) override;
private:
    void refreshList();
    QString selectedName() const;
    void arrangeOwnedWindows(bool cascade);
    void configure(const QString& name);
    void runOperation(const std::function<bool(InstanceManager&)>& operation);
    InstanceManager* m_manager;
    RuntimeRecovery* m_recovery;
    QTableWidget* m_table;
    QLineEdit* m_search{};
    QComboBox *m_statusFilter{}, *m_sortMode{};
    QPointer<QThread> m_job;
    QHash<QString, QPointer<MainWindow>> m_windows;
};
