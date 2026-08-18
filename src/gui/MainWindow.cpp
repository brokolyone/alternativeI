#include "MainWindow.h"

#include <QAction>
#include <QHeaderView>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

namespace gui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), provider_(core::createDefaultProcessProvider()) {
    setWindowTitle(QStringLiteral("Alternative Hacker — Process Manager"));
    resize(1100, 700);

    buildUi();
    buildToolbar();

    connect(&refreshTimer_, &QTimer::timeout, this, &MainWindow::refresh);
    refreshTimer_.start(1000);
    refresh();
}

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    searchBox_ = new QLineEdit(central);
    searchBox_->setPlaceholderText(QStringLiteral("Filter by name, PID or path..."));
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

    layout->addWidget(treeView_);
    setCentralWidget(central);

    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_);
}

void MainWindow::buildToolbar() {
    auto *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);

    auto *refreshAction = toolbar->addAction(QStringLiteral("Refresh"));
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refresh);

    auto *terminateAction = toolbar->addAction(QStringLiteral("Terminate"));
    connect(terminateAction, &QAction::triggered, this, &MainWindow::terminateSelected);
}

void MainWindow::refresh() {
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
    model_->setProcesses(std::move(processes));
    statusLabel_->setText(QStringLiteral("Processes: %1").arg(count));

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
    QAction *terminateAction = menu.addAction(QStringLiteral("Terminate"));

    QMenu *priorityMenu = menu.addMenu(QStringLiteral("Priority"));
    QAction *realtimeAction = priorityMenu->addAction(QStringLiteral("Realtime"));
    QAction *highAction = priorityMenu->addAction(QStringLiteral("High"));
    QAction *aboveNormalAction = priorityMenu->addAction(QStringLiteral("Above normal"));
    QAction *normalAction = priorityMenu->addAction(QStringLiteral("Normal"));
    QAction *belowNormalAction = priorityMenu->addAction(QStringLiteral("Below normal"));
    QAction *idleAction = priorityMenu->addAction(QStringLiteral("Idle"));

    QAction *chosen = menu.exec(treeView_->viewport()->mapToGlobal(pos));
    if (chosen == terminateAction) {
        terminateSelected();
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

    if (QMessageBox::question(this, QStringLiteral("Terminate process"),
                               QStringLiteral("Terminate %1 selected process(es)?")
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
