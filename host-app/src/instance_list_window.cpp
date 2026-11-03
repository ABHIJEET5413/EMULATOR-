#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "instance_list_window.h"
#include "instance_manager.h"
#include "instance_workspace_model.h"
#include "main_window.h"
#include "runtime_recovery.h"
#include "settings_dialog.h"
#include "ui_theme.h"

#include <memory>

InstanceListWindow::InstanceListWindow(InstanceManager* manager, RuntimeRecovery* recovery, QWidget* parent)
    : QWidget(parent), m_manager(manager), m_recovery(recovery)
{
    UiTheme::polishWindow(this, QStringLiteral("sabiInstanceWindow"));
    setWindowTitle("Sabi — Instance Workspace"); resize(1050, 580);
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel("Instance Workspace", this); heading->setObjectName("sabiSettingsTitle"); layout->addWidget(heading);
    auto* filters = new QHBoxLayout;
    m_search = new QLineEdit(this); m_search->setPlaceholderText("Search names and labels"); m_search->setAccessibleName("Search instances"); filters->addWidget(m_search, 1);
    m_statusFilter = new QComboBox(this); m_statusFilter->addItems({"All states", "Running", "Stopped"}); m_statusFilter->setAccessibleName("Instance status filter"); filters->addWidget(m_statusFilter);
    m_sortMode = new QComboBox(this); m_sortMode->addItems({"Sort: Name", "Sort: Status", "Sort: Disk size"}); m_sortMode->setAccessibleName("Instance sort order"); filters->addWidget(m_sortMode);
    layout->addLayout(filters);
    m_table = new QTableWidget(0, 8, this);
    m_table->setHorizontalHeaderLabels({"Instance", "Status", "Labels", "CPU", "RAM (MB)", "Resolution", "Disk (MB)", "Health"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setAlternatingRowColors(true); m_table->setAccessibleName("Sabi instances");
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); m_table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(m_table);
    const auto addRow = [this, layout](const QList<QPair<QString, std::function<void()>>>& actions) {
        auto* row = new QHBoxLayout; layout->addLayout(row);
        for (const auto& entry : actions) { auto* button = new QPushButton(entry.first, this); row->addWidget(button); connect(button, &QPushButton::clicked, this, entry.second); }
    };
    addRow({
        {"Create", [this] { const QString name = QInputDialog::getText(this, "Create instance", "Name (letters, numbers, hyphen, underscore):").trimmed(); if (!name.isEmpty()) runOperation([name](InstanceManager& manager) { return manager.createInstance(name); }); }},
        {"Start", [this] { const auto name = selectedName(); if (!name.isEmpty()) launchInstance(name); }},
        {"Stop", [this] { const auto name = selectedName(); auto window = m_windows.value(name); if (window) window->close(); else if (m_manager->isRunning(name)) QMessageBox::information(this, "Instance running elsewhere", "Stop this instance from the Sabi process that launched it."); }},
        {"Pause / Resume", [this] { auto window = m_windows.value(selectedName()); if (!window || !window->togglePauseResume()) QMessageBox::information(this, "Pause unavailable", "Only a running instance owned by this Sabi workspace can be paused or resumed."); }},
        {"Settings", [this] { configure(selectedName()); }},
        {"Labels", [this] { const QString name = selectedName(); if (name.isEmpty()) return; const auto config = m_manager->getConfig(name); QStringList labels; for (const auto& value : config.value("labels").toArray()) labels << value.toString(); bool accepted = false; const QString text = QInputDialog::getText(this, "Instance labels", "Comma-separated labels (up to eight):", QLineEdit::Normal, labels.join(", "), &accepted); if (accepted) { const auto split = text.split(',', Qt::SkipEmptyParts); QStringList trimmed; for (const QString& label : split) trimmed << label.trimmed(); if (!m_manager->setLabels(name, trimmed)) QMessageBox::warning(this, "Labels not saved", m_manager->lastError()); else refreshList(); } }}
    });
    addRow({
        {"Clone", [this] { const auto source = selectedName(); if (source.isEmpty()) return; const auto name = QInputDialog::getText(this, "Clone " + source, "New instance name:").trimmed(); if (!name.isEmpty()) runOperation([source, name](InstanceManager& manager) { return manager.cloneInstance(source, name); }); }},
        {"Rename", [this] { const QString source = selectedName(); if (source.isEmpty()) return; const QString target = QInputDialog::getText(this, "Rename " + source, "New instance name:", QLineEdit::Normal, source).trimmed(); if (!target.isEmpty() && target != source) runOperation([source, target](InstanceManager& manager) { return manager.renameInstance(source, target); }); }},
        {"Export backup", [this] { const QString name = selectedName(); if (name.isEmpty()) return; const QString root = QFileDialog::getExistingDirectory(this, "Choose empty export destination"); if (!root.isEmpty()) runOperation([name, root](InstanceManager& manager) { return manager.exportInstance(name, root); }); }},
        {"Balanced", [this] { const QString name = selectedName(); if (!name.isEmpty() && !m_manager->applyResourceProfile(name, "balanced")) QMessageBox::warning(this, "Profile not applied", m_manager->lastError()); else refreshList(); }},
        {"Low resource", [this] { const QString name = selectedName(); if (!name.isEmpty() && !m_manager->applyResourceProfile(name, "low")) QMessageBox::warning(this, "Profile not applied", m_manager->lastError()); else refreshList(); }},
        {"Factory reset", [this] { const auto name = selectedName(); if (!name.isEmpty() && QMessageBox::question(this, "Reset " + name, "Erase this instance's apps and data and restore its base image?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) runOperation([name](InstanceManager& manager) { return manager.factoryReset(name); }); }},
        {"Delete", [this] { const auto name = selectedName(); if (!name.isEmpty() && QMessageBox::question(this, "Delete " + name, "Permanently delete all data in this instance?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) runOperation([name](InstanceManager& manager) { return manager.deleteInstance(name); }); }}
    });
    addRow({
        {"Start stopped", [this] { for (const auto& info : m_manager->listInstances()) if (!info.isRunning) launchInstance(info.name); }},
        {"Stop owned", [this] { for (auto window : m_windows) if (window) window->close(); }},
        {"Arrange grid", [this] { arrangeOwnedWindows(false); }},
        {"Arrange cascade", [this] { arrangeOwnedWindows(true); }}
    });
    connect(m_search, &QLineEdit::textChanged, this, &InstanceListWindow::refreshList);
    connect(m_statusFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &InstanceListWindow::refreshList);
    connect(m_sortMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &InstanceListWindow::refreshList);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { launchInstance(selectedName()); });
    auto* refresh = new QTimer(this); refresh->setInterval(2000); connect(refresh, &QTimer::timeout, this, &InstanceListWindow::refreshList); refresh->start();
    refreshList();
}

QString InstanceListWindow::selectedName() const
{
    const auto selected = m_table->selectedItems();
    if (!selected.isEmpty()) return m_table->item(selected.first()->row(), 0)->text();
    const int row = m_table->currentRow(); return row < 0 || !m_table->item(row, 0) ? QString{} : m_table->item(row, 0)->text();
}

InstanceListWindow::~InstanceListWindow()
{
    if (m_job) { m_job->disconnect(this); m_job->wait(); }
    for (auto window : m_windows) if (window) window->disconnect(this);
}

void InstanceListWindow::refreshList()
{
    if (m_job) return;
    const QString selected = selectedName();
    const auto sort = static_cast<InstanceWorkspaceModel::Sort>(m_sortMode->currentIndex());
    const auto list = InstanceWorkspaceModel::filterAndSort(m_manager->listInstances(), m_search->text(), m_statusFilter->currentIndex(), sort);
    m_table->setRowCount(static_cast<int>(list.size()));
    for (int row = 0; row < list.size(); ++row) {
        const auto& info = list.at(row);
        const QStringList cells{info.name, info.isRunning ? "Running" : "Stopped", info.labels.join(", "), QString::number(info.cpuCores), QString::number(info.ramMB), info.resolution, QString::number(info.overlaySize / (1024.0 * 1024.0), 'f', 1), InstanceWorkspaceModel::healthSummary(info)};
        for (int column = 0; column < cells.size(); ++column) m_table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        if (info.name == selected) m_table->selectRow(row);
    }
    if (m_table->currentRow() < 0 && !list.isEmpty()) m_table->selectRow(0);
}

void InstanceListWindow::arrangeOwnedWindows(const bool cascade)
{
    QList<MainWindow*> windows; for (auto window : m_windows) if (window && window->isVisible()) windows.append(window);
    if (windows.isEmpty()) { QMessageBox::information(this, "No owned windows", "Start one or more instances from this workspace before arranging them."); return; }
    const QScreen* target = screen() ? screen() : QGuiApplication::primaryScreen(); const QRect area = target ? target->availableGeometry() : QRect(0, 0, 1280, 720);
    if (cascade) {
        for (int index = 0; index < windows.size(); ++index) windows.at(index)->setGeometry(area.x() + 32 * index, area.y() + 32 * index, qMax(480, area.width() * 2 / 3), qMax(500, area.height() * 2 / 3));
        return;
    }
    const int columns = qCeil(qSqrt(static_cast<qreal>(windows.size()))); const int rows = qCeil(static_cast<qreal>(windows.size()) / columns);
    const QSize cell(area.width() / columns, area.height() / rows);
    for (int index = 0; index < windows.size(); ++index) windows.at(index)->setGeometry(QRect(area.x() + (index % columns) * cell.width(), area.y() + (index / columns) * cell.height(), cell.width(), cell.height()));
}

void InstanceListWindow::launchInstance(const QString& name, const QString& apk)
{
    if (name.isEmpty()) return;
    if (m_job) { connect(m_job, &QThread::finished, this, [this, name, apk] { launchInstance(name, apk); }, Qt::QueuedConnection); return; }
    if (auto window = m_windows.value(name)) { if (!m_manager->isRunning(name)) { connect(window, &QObject::destroyed, this, [this, name, apk] { launchInstance(name, apk); }); window->close(); return; } window->showNormal(); window->raise(); window->activateWindow(); if (!apk.isEmpty()) window->queueApkInstall(apk); return; }
    if (!m_manager->acquireInstance(name)) { show(); QMessageBox::warning(this, "Cannot start instance", m_manager->lastError()); return; }
    auto* window = new MainWindow(m_manager, name, m_recovery, this); window->setWindowFlag(Qt::Window); window->setAttribute(Qt::WA_DeleteOnClose); m_windows.insert(name, window);
    connect(window, &MainWindow::instanceListRequested, this, [this] { showNormal(); raise(); activateWindow(); });
    connect(window, &QObject::destroyed, this, [this, name] { m_windows.remove(name); refreshList(); if (!isVisible()) show(); });
    window->show(); if (!apk.isEmpty()) window->queueApkInstall(apk); refreshList();
}

void InstanceListWindow::configure(const QString& name)
{
    if (m_job || name.isEmpty()) return;
    SettingsDialog dialog(m_manager->getConfig(name), m_manager->isRunning(name), this);
    while (dialog.exec() == QDialog::Accepted) { if (m_manager->setConfig(name, dialog.getConfig())) { refreshList(); break; } QMessageBox::warning(this, "Settings not saved", m_manager->lastError()); }
}

void InstanceListWindow::runOperation(const std::function<bool(InstanceManager&)>& operation)
{
    if (m_job) return;
    struct Result { bool success{false}; QString error; }; const auto result = std::make_shared<Result>(); const auto root = m_manager->resourceRoot(), instances = m_manager->instancesRoot();
    setEnabled(false); setWindowTitle("Sabi — Working on instance disk...");
    m_job = QThread::create([root, instances, operation, result] { InstanceManager worker(root, instances); result->success = operation(worker); result->error = worker.lastError(); }); m_job->setParent(this);
    connect(m_job, &QThread::finished, this, [this, result] { auto thread = m_job; m_job = nullptr; if (thread) thread->deleteLater(); setEnabled(true); setWindowTitle("Sabi — Instance Workspace"); refreshList(); if (!result->success) QMessageBox::warning(this, "Instance operation failed", result->error); }); m_job->start();
}

void InstanceListWindow::closeEvent(QCloseEvent* event)
{
    if (m_job) { event->ignore(); return; }
    if (!m_windows.isEmpty()) { hide(); event->ignore(); return; }
    event->accept();
}
