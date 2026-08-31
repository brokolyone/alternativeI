#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <vector>

#include "AboutDialog.h"
#include "AffinityDialog.h"
#include "DiskToolsView.h"
#include "NetworkView.h"
#include "PerformanceView.h"
#include "ProcessDetailsDialog.h"
#include "ServicesView.h"
#include "SettingsDialog.h"
#include "StartupProgramsView.h"
#include "SystemInfoView.h"
#include "i18n.h"

namespace gui {

namespace {
constexpr const char *kRefreshIntervalKey = "refreshIntervalMs";

QString priorityToCsvString(core::ProcessPriority priority) {
    switch (priority) {
        case core::ProcessPriority::Idle: return QStringLiteral("Idle");
        case core::ProcessPriority::BelowNormal: return QStringLiteral("BelowNormal");
        case core::ProcessPriority::Normal: return QStringLiteral("Normal");
        case core::ProcessPriority::AboveNormal: return QStringLiteral("AboveNormal");
        case core::ProcessPriority::High: return QStringLiteral("High");
        case core::ProcessPriority::Realtime: return QStringLiteral("Realtime");
        default: return QStringLiteral("Unknown");
    }
}

// RFC 4180: a field containing a comma, quote or newline must be quoted,
// with internal quotes doubled.
QString csvField(const QString &value) {
    if (!value.contains(',') && !value.contains('"') && !value.contains('\n')) {
        return value;
    }
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), provider_(core::createDefaultProcessProvider()) {
    setWindowTitle(i18n::t("AltTools — Process Manager", "AltTools — Диспетчер процессов"));
    resize(1100, 700);

    refreshIntervalMs_ = QSettings().value(QLatin1String(kRefreshIntervalKey), 1000).toInt();

    buildUi();
    buildToolbar();

    connect(&refreshTimer_, &QTimer::timeout, this, &MainWindow::refresh);
    refreshTimer_.start(refreshIntervalMs_);
    refresh();
}

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    searchBox_ = new QLineEdit(central);
    searchBox_->setPlaceholderText(i18n::t("Filter by name, PID or path...", "Фильтр по имени, PID или пути..."));
    layout->addWidget(searchBox_);

    model_ = new ProcessTreeModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(-1); // search across all columns
    proxyModel_->setRecursiveFilteringEnabled(true);

    connect(searchBox_, &QLineEdit::textChanged, proxyModel_,
            &QSortFilterProxyModel::setFilterFixedString);

    treeView_ = new QTreeView(central);
    treeView_->setModel(proxyModel_);
    treeView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setSortingEnabled(true);
    treeView_->setAlternatingRowColors(true);
    treeView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    treeView_->setUniformRowHeights(true);
    treeView_->header()->setStretchLastSection(true);
    treeView_->header()->setSectionResizeMode(ProcessTreeModel::ColumnName, QHeaderView::Stretch);
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(treeView_, &QTreeView::customContextMenuRequested, this,
            &MainWindow::showProcessContextMenu);
    connect(treeView_, &QTreeView::expanded, this, &MainWindow::onRowExpanded);
    connect(treeView_, &QTreeView::collapsed, this, &MainWindow::onRowCollapsed);
    connect(treeView_, &QTreeView::doubleClicked, this, &MainWindow::openPropertiesForIndex);

    treeView_->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(treeView_->header(), &QHeaderView::customContextMenuRequested, this,
            &MainWindow::showColumnContextMenu);
    restoreColumnVisibility();

    layout->addWidget(treeView_);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(central, i18n::t("Processes", "Процессы"));
    tabs->addTab(new PerformanceView(tabs), i18n::t("Performance", "Производительность"));
    tabs->addTab(new ServicesView(tabs), i18n::t("Services", "Службы"));
    tabs->addTab(new NetworkView(tabs), i18n::t("Network", "Сеть"));
    tabs->addTab(new DiskToolsView(tabs), i18n::t("Disk", "Диск"));
    tabs->addTab(new StartupProgramsView(tabs), i18n::t("Startup", "Автозагрузка"));
    tabs->addTab(new SystemInfoView(tabs), i18n::t("System", "Система"));
    setCentralWidget(tabs);

    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_);
}

void MainWindow::buildToolbar() {
    auto *toolbar = addToolBar(i18n::t("Main", "Основная"));
    toolbar->setMovable(false);

    auto *refreshAction = toolbar->addAction(i18n::t("Refresh", "Обновить"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refresh);

    auto *propertiesAction = toolbar->addAction(i18n::t("Properties", "Свойства"));
    connect(propertiesAction, &QAction::triggered, this, &MainWindow::openPropertiesForSelected);

    auto *terminateAction = toolbar->addAction(i18n::t("Terminate", "Завершить"));
    connect(terminateAction, &QAction::triggered, this, &MainWindow::terminateSelected);

    auto *exportCsvAction = toolbar->addAction(i18n::t("Export CSV...", "Экспорт CSV..."));
    connect(exportCsvAction, &QAction::triggered, this, &MainWindow::exportProcessListToCsv);

    toolbar->addSeparator();

    auto *settingsAction = toolbar->addAction(i18n::t("Settings", "Настройки"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettingsDialog);

    auto *aboutAction = toolbar->addAction(i18n::t("About", "О программе"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::showAboutDialog() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::showSettingsDialog() {
    SettingsDialog dialog(refreshIntervalMs_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    refreshIntervalMs_ = dialog.refreshIntervalMs();
    QSettings().setValue(QLatin1String(kRefreshIntervalKey), refreshIntervalMs_);
    refreshTimer_.setInterval(refreshIntervalMs_);
}

void MainWindow::exportProcessListToCsv() {
    const QString path = QFileDialog::getSaveFileName(
        this, i18n::t("Export process list", "Экспорт списка процессов"), QString(),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, i18n::t("Export failed", "Ошибка экспорта"),
                              i18n::t("Could not open the file for writing.",
                                      "Не удалось открыть файл для записи."));
        return;
    }

    QTextStream out(&file);
    out << "PID,PPID,Name,User,CPU%,MemoryBytes,Threads,Priority,Path,CommandLine\n";
    for (const core::ProcessInfo *info : model_->allProcesses()) {
        out << info->pid << ',' << info->ppid << ',' << csvField(QString::fromStdString(info->name))
            << ',' << csvField(QString::fromStdString(info->user)) << ','
            << QString::number(info->cpuPercent, 'f', 1) << ',' << info->privateBytes << ','
            << info->threadCount << ',' << priorityToCsvString(info->priority) << ','
            << csvField(QString::fromStdString(info->exePath)) << ','
            << csvField(QString::fromStdString(info->commandLine)) << '\n';
    }
}

void MainWindow::refresh() {
    // Skip the periodic refresh while Ctrl is held: that's the modifier used
    // to multi-select rows in the tree, and re-sorting/rebuilding the model
    // out from under the pointer mid-Ctrl-click made it near-impossible to
    // land a click on a specific row. Releasing Ctrl lets refreshes (at the
    // interval configured in Settings) resume as normal.
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        return;
    }

    QSet<uint64_t> selectedPids;
    if (treeView_->selectionModel()) {
        const auto selectedRows = treeView_->selectionModel()->selectedRows();
        for (const QModelIndex &proxyIndex : selectedRows) {
            const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
            if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
                selectedPids.insert(info->pid);
            }
        }
    }

    auto processes = provider_->snapshot();
    const int count = static_cast<int>(processes.size());

    QSet<uint64_t> currentPids;
    currentPids.reserve(count);
    for (const auto &proc : processes) {
        currentPids.insert(proc.pid);
    }
    // Skip highlighting on the very first snapshot - every process would
    // count as "new" against an empty knownPids_, flashing the whole tree.
    if (autoExpandDone_) {
        QSet<uint64_t> newPids = currentPids;
        newPids.subtract(knownPids_);
        model_->setHighlightedPids(newPids);
    }
    knownPids_ = currentPids;

    model_->setProcesses(std::move(processes));
    statusLabel_->setText(i18n::t("Processes: %1", "Процессов: %1").arg(count));

    if (!autoExpandDone_) {
        treeView_->expandAll();
        autoExpandDone_ = true;
    } else {
        restoreTreeState();
    }

    if (!selectedPids.isEmpty() && treeView_->selectionModel()) {
        QItemSelection selection;
        for (uint64_t pid : selectedPids) {
            const QModelIndex sourceIndex = model_->indexForPid(pid);
            if (!sourceIndex.isValid()) continue;
            const QModelIndex proxyIndex = proxyModel_->mapFromSource(sourceIndex);
            if (proxyIndex.isValid()) {
                const QModelIndex lastColumn = proxyIndex.siblingAtColumn(ProcessTreeModel::ColumnCount - 1);
                selection.select(proxyIndex, lastColumn);
            }
        }
        if (!selection.isEmpty()) {
            treeView_->selectionModel()->select(
                selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
    }
}

void MainWindow::showColumnContextMenu(const QPoint &pos) {
    QMenu menu(this);
    for (int column = 0; column < ProcessTreeModel::ColumnCount; ++column) {
        if (column == ProcessTreeModel::ColumnName) {
            continue; // always visible - it's where the tree structure lives
        }
        const QString label = model_->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(!treeView_->isColumnHidden(column));
        connect(action, &QAction::toggled, this, [this, column](bool visible) {
            treeView_->setColumnHidden(column, !visible);
            saveColumnVisibility();
        });
    }
    menu.exec(treeView_->header()->mapToGlobal(pos));
}

void MainWindow::saveColumnVisibility() {
    QVariantList hidden;
    for (int column = 0; column < ProcessTreeModel::ColumnCount; ++column) {
        if (treeView_->isColumnHidden(column)) {
            hidden.push_back(column);
        }
    }
    QSettings().setValue(QStringLiteral("hiddenColumns"), hidden);
}

void MainWindow::restoreColumnVisibility() {
    // Command Line defaults to hidden (long, noisy, rarely needed at a
    // glance) unless the user has an explicit saved preference - once
    // they've touched the header menu at all, saveColumnVisibility() has
    // written the *complete* actual hidden set, so this default only ever
    // applies on a genuinely first run.
    static const QVariantList kDefaultHidden = {ProcessTreeModel::ColumnCommandLine};
    const QVariantList hiddenList =
        QSettings().value(QStringLiteral("hiddenColumns"), kDefaultHidden).toList();

    QSet<int> hidden;
    for (const QVariant &value : hiddenList) {
        hidden.insert(value.toInt());
    }
    for (int column = ProcessTreeModel::ColumnName + 1; column < ProcessTreeModel::ColumnCount; ++column) {
        treeView_->setColumnHidden(column, hidden.contains(column));
    }
}

void MainWindow::restoreTreeState() {
    for (uint64_t pid : std::as_const(expandedPids_)) {
        const QModelIndex sourceIndex = model_->indexForPid(pid);
        if (!sourceIndex.isValid()) continue;
        const QModelIndex proxyIndex = proxyModel_->mapFromSource(sourceIndex);
        if (proxyIndex.isValid()) {
            treeView_->setExpanded(proxyIndex, true);
        }
    }
}

void MainWindow::onRowExpanded(const QModelIndex &proxyIndex) {
    const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
    if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
        expandedPids_.insert(info->pid);
    }
}

void MainWindow::onRowCollapsed(const QModelIndex &proxyIndex) {
    const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
    if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
        expandedPids_.remove(info->pid);
    }
}

void MainWindow::showProcessContextMenu(const QPoint &pos) {
    const QModelIndex index = treeView_->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *propertiesAction = menu.addAction(i18n::t("Properties...", "Свойства..."));
    menu.addSeparator();
    QAction *terminateAction = menu.addAction(i18n::t("Terminate", "Завершить"));
    QAction *terminateTreeAction = menu.addAction(i18n::t("Terminate process tree", "Завершить дерево процессов"));
    menu.addSeparator();
    QAction *suspendAction = menu.addAction(i18n::t("Suspend", "Приостановить"));
    QAction *resumeAction = menu.addAction(i18n::t("Resume", "Возобновить"));
    QAction *affinityAction = menu.addAction(i18n::t("Set Affinity...", "Привязка к процессорам..."));
    menu.addSeparator();
    QAction *openLocationAction = menu.addAction(i18n::t("Open File Location", "Открыть расположение файла"));
    QAction *copyInfoAction = menu.addAction(i18n::t("Copy Process Info", "Копировать информацию о процессе"));

    QMenu *priorityMenu = menu.addMenu(i18n::t("Priority", "Приоритет"));
    QAction *realtimeAction = priorityMenu->addAction(i18n::t("Realtime", "Реального времени"));
    QAction *highAction = priorityMenu->addAction(i18n::t("High", "Высокий"));
    QAction *aboveNormalAction = priorityMenu->addAction(i18n::t("Above normal", "Выше среднего"));
    QAction *normalAction = priorityMenu->addAction(i18n::t("Normal", "Средний"));
    QAction *belowNormalAction = priorityMenu->addAction(i18n::t("Below normal", "Ниже среднего"));
    QAction *idleAction = priorityMenu->addAction(i18n::t("Idle", "Простой"));

    QAction *chosen = menu.exec(treeView_->viewport()->mapToGlobal(pos));
    if (chosen == propertiesAction) {
        openPropertiesForIndex(index);
    } else if (chosen == terminateAction) {
        terminateSelected();
    } else if (chosen == terminateTreeAction) {
        terminateSelectedTrees();
    } else if (chosen == suspendAction) {
        suspendSelected();
    } else if (chosen == resumeAction) {
        resumeSelected();
    } else if (chosen == affinityAction) {
        setAffinityForSelected();
    } else if (chosen == openLocationAction) {
        openFileLocationForSelected();
    } else if (chosen == copyInfoAction) {
        copyProcessInfoForSelected();
    } else if (chosen == realtimeAction) {
        setPrioritySelected(core::ProcessPriority::Realtime);
    } else if (chosen == highAction) {
        setPrioritySelected(core::ProcessPriority::High);
    } else if (chosen == aboveNormalAction) {
        setPrioritySelected(core::ProcessPriority::AboveNormal);
    } else if (chosen == normalAction) {
        setPrioritySelected(core::ProcessPriority::Normal);
    } else if (chosen == belowNormalAction) {
        setPrioritySelected(core::ProcessPriority::BelowNormal);
    } else if (chosen == idleAction) {
        setPrioritySelected(core::ProcessPriority::Idle);
    }
}

void MainWindow::terminateSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    if (QMessageBox::question(this, i18n::t("Terminate process", "Завершение процесса"),
                               i18n::t("Terminate %1 selected process(es)?",
                                       "Завершить выбранные процессы (%1)?")
                                   .arg(selected.size())) != QMessageBox::Yes) {
        return;
    }

    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            provider_->terminate(info->pid);
        }
    }
    refresh();
}

void MainWindow::terminateSelectedTrees() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    // Snapshot pid + all descendants for every selected row *before*
    // terminating anything - killing a parent first would otherwise orphan
    // its children in the tree we're about to re-walk mid-loop.
    QSet<uint64_t> pids;
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            pids.insert(info->pid);
            for (uint64_t descendant : model_->descendantPids(info->pid)) {
                pids.insert(descendant);
            }
        }
    }
    if (pids.isEmpty()) {
        return;
    }

    if (QMessageBox::question(
            this, i18n::t("Terminate process tree", "Завершение дерева процессов"),
            i18n::t("Terminate %1 process(es) (selected plus all children)?",
                    "Завершить %1 процесс(ов) (выбранные и все их дочерние)?")
                .arg(pids.size())) != QMessageBox::Yes) {
        return;
    }

    for (uint64_t pid : std::as_const(pids)) {
        provider_->terminate(pid);
    }
    refresh();
}

void MainWindow::suspendSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            provider_->suspend(info->pid);
        }
    }
    refresh();
}

void MainWindow::resumeSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            provider_->resume(info->pid);
        }
    }
    refresh();
}

void MainWindow::setAffinityForSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    std::vector<uint64_t> pids;
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            pids.push_back(info->pid);
        }
    }
    if (pids.empty()) {
        return;
    }

    // Pre-check the first selected process's current mask; applied to
    // every selected process on OK (PH itself only supports a single
    // process at a time here - this is a deliberate superset).
    AffinityDialog dialog(provider_->affinityMask(pids.front()), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const uint64_t mask = dialog.selectedMask();
    for (uint64_t pid : pids) {
        provider_->setAffinityMask(pid, mask);
    }
}

void MainWindow::openFileLocationForSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }
    const QModelIndex sourceIndex = proxyModel_->mapToSource(selected.first());
    const core::ProcessInfo *info = model_->processForIndex(sourceIndex);
    if (info == nullptr || info->exePath.empty()) {
        return;
    }

    const QFileInfo fileInfo(QString::fromStdString(info->exePath));
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileInfo.absolutePath()));
}

void MainWindow::copyProcessInfoForSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (selected.isEmpty()) {
        return;
    }

    QString text;
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        const core::ProcessInfo *info = model_->processForIndex(sourceIndex);
        if (info == nullptr) {
            continue;
        }
        text += QStringLiteral("%1\tPID %2\tPPID %3\t%4\t%5\n")
                    .arg(QString::fromStdString(info->name))
                    .arg(info->pid)
                    .arg(info->ppid)
                    .arg(QString::fromStdString(info->user), QString::fromStdString(info->exePath));
    }
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void MainWindow::openPropertiesForSelected() {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    if (!selected.isEmpty()) {
        openPropertiesForIndex(selected.first());
    }
}

void MainWindow::openPropertiesForIndex(const QModelIndex &proxyIndex) {
    if (!proxyIndex.isValid()) {
        return;
    }
    const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
    const core::ProcessInfo *info = model_->processForIndex(sourceIndex);
    if (info == nullptr) {
        return;
    }

    auto *dialog = new ProcessDetailsDialog(provider_.get(), info->pid,
                                             QString::fromStdString(info->name), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::setPrioritySelected(core::ProcessPriority priority) {
    const QModelIndexList selected = treeView_->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processForIndex(sourceIndex)) {
            provider_->setPriority(info->pid, priority);
        }
    }
    refresh();
}

} // namespace gui
