#pragma once

#include <QTimer>
#include <QWidget>
#include <memory>

#include "../core/IServiceManager.h"
#include "ServicesTableModel.h"

class QLineEdit;
class QTableView;
class QSortFilterProxyModel;

namespace gui {

class ServicesView : public QWidget {
    Q_OBJECT

public:
    explicit ServicesView(QWidget *parent = nullptr);

private slots:
    void refresh();
    void showContextMenu(const QPoint &pos);

private:
    void controlSelected(bool (core::IServiceManager::*action)(const std::string &));

    std::unique_ptr<core::IServiceManager> manager_;
    ServicesTableModel *model_;
    QSortFilterProxyModel *proxyModel_;
    QTableView *tableView_;
    QLineEdit *searchBox_;
    QTimer refreshTimer_;
};

} // namespace gui
