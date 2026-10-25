#include <QCloseEvent>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "instance_list_window.h"
#include "instance_manager.h"
#include "main_window.h"
#include "settings_dialog.h"

#include <memory>

InstanceListWindow::InstanceListWindow(InstanceManager* manager, QWidget* parent)
    : QWidget(parent), m_manager(manager)
{
    setWindowTitle("Sabi - Instances"); resize(850, 390);
    auto* layout = new QVBoxLayout(this);
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({"Instance", "Status", "CPU", "RAM (MB)", "Resolution", "Disk (MB)"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_table);
    auto* buttons = new QHBoxLayout; layout->addLayout(buttons);
    const auto add = [this, buttons](const QString& text, const std::function<void()>& action) {
        auto* button = new QPushButton(text, this); buttons->addWidget(button);
        connect(button, &QPushButton::clicked, this, action);
    };
    add("Create", [this] {
        const QString name = QInputDialog::getText(this, "Create instance", "Name (letters, numbers, hyphen, underscore):").trimmed();
        if (!name.isEmpty()) runOperation([name](InstanceManager& manager) { return manager.createInstance(name); });
    });
    add("Start", [this] { const auto name = selectedName(); if (!name.isEmpty()) launchInstance(name); });
    add("Stop", [this] {
        const auto name = selectedName(); auto window = m_windows.value(name);
        if (window) window->close();
        else if (m_manager->isRunning(name)) QMessageBox::information(this, "Instance running elsewhere", "Stop this instance from the Sabi process that launched it.");
    });
    add("Settings", [this] { configure(selectedName()); });
    add("Clone", [this] {
        const auto source = selectedName(); if (source.isEmpty()) return;
        const auto name = QInputDialog::getText(this, "Clone " + source, "New instance name:").trimmed();
        if (!name.isEmpty()) runOperation([source, name](InstanceManager& manager) { return manager.cloneInstance(source, name); });
    });
    add("Delete", [this] {
        const auto name = selectedName(); if (name.isEmpty()) return;
        if (QMessageBox::question(this, "Delete " + name, "Permanently delete all data in this instance?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            runOperation([name](InstanceManager& manager) { return manager.deleteInstance(name); });
    });
    add("Factory reset", [this] {
        const auto name = selectedName(); if (name.isEmpty()) return;
        if (QMessageBox::question(this, "Reset " + name, "Erase this instance's apps and data and restore its base image?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            runOperation([name](InstanceManager& manager) { return manager.factoryReset(name); });
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { launchInstance(selectedName()); });
    auto* refresh = new QTimer(this); refresh->setInterval(2000);
    connect(refresh, &QTimer::timeout, this, &InstanceListWindow::refreshList); refresh->start();
    refreshList();
}

QString InstanceListWindow::selectedName() const
{
    const auto selected = m_table->selectedItems();
    if (!selected.isEmpty()) return m_table->item(selected.first()->row(), 0)->text();
    const int row = m_table->currentRow();
    return row < 0 || !m_table->item(row, 0) ? QString{} : m_table->item(row, 0)->text();
}

InstanceListWindow::~InstanceListWindow()
{
    if (m_job) { m_job->disconnect(this); m_job->wait(); }
    for (auto window : m_windows) if (window) window->disconnect(this);
}

void InstanceListWindow::refreshList()
{
    if (m_job) return;
    const auto selected = selectedName(); const auto list = m_manager->listInstances();
    m_table->setRowCount(static_cast<int>(list.size()));
    for (int row = 0; row < list.size(); ++row) {
        const auto& info = list.at(row);
        const QStringList cells{info.name, info.isRunning ? "Running" : "Stopped", QString::number(info.cpuCores),
                                QString::number(info.ramMB), info.resolution, QString::number(info.overlaySize / (1024.0 * 1024.0), 'f', 1)};
        for (int column = 0; column < cells.size(); ++column) m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        if (info.name == selected) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && !list.isEmpty()) m_table->selectRow(0);
}

void InstanceListWindow::launchInstance(const QString& name, const QString& apk)
{
    if (name.isEmpty()) return;
    if (m_job) {
        // Preserve APK/launch requests received while a clone/create is busy.
        connect(m_job, &QThread::finished, this, [this, name, apk] { launchInstance(name, apk); }, Qt::QueuedConnection);
        return;
    }
    if (auto window = m_windows.value(name)) {
        if (!m_manager->isRunning(name)) {
            connect(window, &QObject::destroyed, this, [this, name, apk] { launchInstance(name, apk); });
            window->close(); return;
        }
        window->showNormal(); window->raise(); window->activateWindow();
        if (!apk.isEmpty()) window->queueApkInstall(apk); return;
    }
    if (!m_manager->acquireInstance(name)) { show(); QMessageBox::warning(this, "Cannot start instance", m_manager->lastError()); return; }
    auto* window = new MainWindow(m_manager, name, this);
    window->setWindowFlag(Qt::Window); window->setAttribute(Qt::WA_DeleteOnClose);
    m_windows.insert(name, window);
    connect(window, &MainWindow::instanceListRequested, this, [this] { showNormal(); raise(); activateWindow(); });
    connect(window, &QObject::destroyed, this, [this, name] { m_windows.remove(name); refreshList(); if (!isVisible()) show(); });
    window->show(); if (!apk.isEmpty()) window->queueApkInstall(apk);
    refreshList();
}

void InstanceListWindow::configure(const QString& name)
{
    if (m_job || name.isEmpty()) return;
    SettingsDialog dialog(m_manager->getConfig(name), m_manager->isRunning(name), this);
    while (dialog.exec() == QDialog::Accepted) {
        if (m_manager->setConfig(name, dialog.getConfig())) { refreshList(); break; }
        QMessageBox::warning(this, "Settings not saved", m_manager->lastError());
    }
}

void InstanceListWindow::runOperation(const std::function<bool(InstanceManager&)>& operation)
{
    if (m_job) return;
    struct Result { bool success{false}; QString error; };
    const auto result = std::make_shared<Result>();
    const auto root = m_manager->resourceRoot(), instances = m_manager->instancesRoot();
    setEnabled(false); setWindowTitle("Sabi - Working on instance disk...");
    m_job = QThread::create([root, instances, operation, result] {
        InstanceManager worker(root, instances);
        result->success = operation(worker); result->error = worker.lastError();
    });
    m_job->setParent(this);
    connect(m_job, &QThread::finished, this, [this, result] {
        auto thread = m_job; m_job = nullptr;
        if (thread) thread->deleteLater();
        setEnabled(true); setWindowTitle("Sabi - Instances"); refreshList();
        if (!result->success) QMessageBox::warning(this, "Instance operation failed", result->error);
    });
    m_job->start();
}

void InstanceListWindow::closeEvent(QCloseEvent* event)
{
    if (m_job) { event->ignore(); return; }
    if (!m_windows.isEmpty()) { hide(); event->ignore(); return; }
    event->accept();
}
