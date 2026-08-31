#pragma once

#include <QAbstractTableModel>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <vector>

#include "../core/IProcessProvider.h"

class QLineEdit;
class QTableView;
class QSortFilterProxyModel;

namespace gui {

// Every process' network connections tagged with the owning process'
// name/pid - a system-wide equivalent of the per-process Network tab in
// ProcessDetailsDialog, like Process Hacker's dedicated Network tab.
class SystemNetworkTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColumnProcess = 0,
        ColumnPid,
        ColumnProtocol,
        ColumnLocalAddress,
        ColumnLocalPort,
        ColumnRemoteAddress,
        ColumnRemotePort,
        ColumnState,
        ColumnCount,
    };

    struct Entry {
        QString processName;
        uint64_t pid = 0;
        core::NetworkConnectionInfo connection;
    };

    explicit SystemNetworkTableModel(QObject *parent = nullptr);

    void setEntries(std::vector<Entry> entries);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<Entry> entries_;
};

// "Network" tab: polls every process' connections on its own timer (this
// is a heavier scan than the main list's snapshot(), so it runs at a
// slower, independent interval) and flattens them into one system-wide
// table via SystemNetworkTableModel.
class NetworkView : public QWidget {
    Q_OBJECT

public:
    explicit NetworkView(QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    std::unique_ptr<core::IProcessProvider> provider_;
    SystemNetworkTableModel *model_;
    QSortFilterProxyModel *proxyModel_;
    QTableView *tableView_;
    QLineEdit *searchBox_;
    QTimer refreshTimer_;
};

} // namespace gui
