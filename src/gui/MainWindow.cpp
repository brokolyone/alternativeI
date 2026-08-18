#include "MainWindow.h"

#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
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

    model_ = new ProcessTableModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(-1); // search across all columns

    connect(searchBox_, &QLineEdit::textChanged, proxyModel_,
            &QSortFilterProxyModel::setFilterFixedString);

    tableView_ = new QTableView(central);
    tableView_->setModel(proxyModel_);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableView_->setSortingEnabled(true);
    tableView_->setAlternatingRowColors(true);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(ProcessTableModel::ColumnName,
                                                           QHeaderView::Stretch);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableView_, &QTableView::customContextMenuRequested, this,
            &MainWindow::showProcessContextMenu);

    layout->addWidget(tableView_);
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
    auto processes = provider_->snapshot();
    const int count = static_cast<int>(processes.size());
    model_->setProcesses(std::move(processes));
    statusLabel_->setText(QStringLiteral("Processes: %1").arg(count));
}

void MainWindow::showProcessContextMenu(const QPoint &pos) {
    const QModelIndex index = tableView_->indexAt(pos);
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

    QAction *chosen = menu.exec(tableView_->viewport()->mapToGlobal(pos));
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
    const QModelIndexList selected = tableView_->selectionModel()->selectedRows();
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
        if (const core::ProcessInfo *info = model_->processAt(sourceIndex.row())) {
            provider_->terminate(info->pid);
        }
    }
    refresh();
}

void MainWindow::setPrioritySelected(core::ProcessPriority priority) {
    const QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ProcessInfo *info = model_->processAt(sourceIndex.row())) {
            provider_->setPriority(info->pid, priority);
        }
    }
    refresh();
}

} // namespace gui
