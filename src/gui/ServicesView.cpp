#include "ServicesView.h"

#include <QAction>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include "i18n.h"

namespace gui {

ServicesView::ServicesView(QWidget *parent)
    : QWidget(parent), manager_(core::createDefaultServiceManager()) {
    auto *layout = new QVBoxLayout(this);

    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText(i18n::t("Filter services...", "Фильтр служб..."));
    layout->addWidget(searchBox_);

    model_ = new ServicesTableModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(-1);
    connect(searchBox_, &QLineEdit::textChanged, proxyModel_,
            &QSortFilterProxyModel::setFilterFixedString);

    tableView_ = new QTableView(this);
    tableView_->setModel(proxyModel_);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSortingEnabled(true);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableView_, &QTableView::customContextMenuRequested, this, &ServicesView::showContextMenu);
    layout->addWidget(tableView_);

    connect(&refreshTimer_, &QTimer::timeout, this, &ServicesView::refresh);
    refreshTimer_.start(2000);
    refresh();
}

void ServicesView::refresh() {
    model_->setServices(manager_->list());
}

void ServicesView::showContextMenu(const QPoint &pos) {
    const QModelIndex index = tableView_->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *startAction = menu.addAction(i18n::t("Start", "Запустить"));
    QAction *stopAction = menu.addAction(i18n::t("Stop", "Остановить"));
    QAction *restartAction = menu.addAction(i18n::t("Restart", "Перезапустить"));

    QAction *chosen = menu.exec(tableView_->viewport()->mapToGlobal(pos));
    if (chosen == startAction) {
        controlSelected(&core::IServiceManager::start);
    } else if (chosen == stopAction) {
        controlSelected(&core::IServiceManager::stop);
    } else if (chosen == restartAction) {
        controlSelected(&core::IServiceManager::restart);
    }
}

void ServicesView::controlSelected(bool (core::IServiceManager::*action)(const std::string &)) {
    const QModelIndexList selected = tableView_->selectionModel()->selectedRows();
    bool anyFailed = false;
    for (const QModelIndex &proxyIndex : selected) {
        const QModelIndex sourceIndex = proxyModel_->mapToSource(proxyIndex);
        if (const core::ServiceInfo *service = model_->serviceAt(sourceIndex.row())) {
            if (!(manager_.get()->*action)(service->name)) {
                anyFailed = true;
            }
        }
    }
    if (anyFailed) {
        QMessageBox::warning(this, i18n::t("Service control", "Управление службами"),
                              i18n::t("One or more operations failed - this usually means "
                                      "insufficient privileges (run elevated/as root).",
                                      "Одна или несколько операций завершились неудачей — обычно это "
                                      "означает нехватку прав (запустите от администратора/root)."));
    }
    refresh();
}

} // namespace gui
