#pragma once

#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <memory>

#include "../core/IProcessProvider.h"
#include "ProcessTableModel.h"

class QLineEdit;
class QTableView;
class QLabel;

namespace gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refresh();
    void showProcessContextMenu(const QPoint &pos);
    void terminateSelected();
    void setPrioritySelected(core::ProcessPriority priority);

private:
    void buildUi();
    void buildToolbar();

    std::unique_ptr<core::IProcessProvider> provider_;
    ProcessTableModel *model_;
    QSortFilterProxyModel *proxyModel_;
    QTableView *tableView_;
    QLineEdit *searchBox_;
    QLabel *statusLabel_;
    QTimer refreshTimer_;
};

} // namespace gui
