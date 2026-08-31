#include "StartupProgramsView.h"

#include <QHeaderView>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include "StartupPrograms.h"
#include "i18n.h"

namespace gui {

StartupProgramsView::StartupProgramsView(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    auto *refreshButton = new QPushButton(i18n::t("Refresh", "Обновить"), this);
    connect(refreshButton, &QPushButton::clicked, this, &StartupProgramsView::reload);
    layout->addWidget(refreshButton, 0, Qt::AlignLeft);

    model_ = new QStandardItemModel(0, 3, this);
    model_->setHorizontalHeaderLabels({i18n::t("Name", "Имя"), i18n::t("Command", "Команда"),
                                        i18n::t("Source", "Источник")});

    tableView_ = new QTableView(this);
    tableView_->setModel(model_);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSortingEnabled(true);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->verticalHeader()->setVisible(false);
    layout->addWidget(tableView_);

    reload();
}

void StartupProgramsView::reload() {
    model_->removeRows(0, model_->rowCount());
    for (const StartupEntry &entry : enumerateStartupPrograms()) {
        const int row = model_->rowCount();
        model_->insertRow(row);
        model_->setItem(row, 0, new QStandardItem(entry.name));
        model_->setItem(row, 1, new QStandardItem(entry.command));
        model_->setItem(row, 2, new QStandardItem(entry.source));
    }
}

} // namespace gui
