#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "../core/ProcessDetails.h"

namespace gui {

// Small read-only table models backing the tabs of ProcessDetailsDialog.
// Each just wraps a vector<T> from core::IProcessProvider - there's no
// shared behavior worth a template base (Qt's moc can't handle templated
// QObjects anyway), so these stay as plain, separate classes.

class ThreadsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColumnTid = 0, ColumnState, ColumnPriority, ColumnCount };
    explicit ThreadsTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setData(std::vector<core::ThreadInfo> items);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::ThreadInfo> items_;
};

class ModulesTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColumnName = 0, ColumnBase, ColumnSize, ColumnPath, ColumnCount };
    explicit ModulesTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setData(std::vector<core::ModuleInfo> items);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::ModuleInfo> items_;
};

class MemoryRegionsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColumnBase = 0, ColumnSize, ColumnProtection, ColumnMappedFile, ColumnCount };
    explicit MemoryRegionsTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setData(std::vector<core::MemoryRegionInfo> items);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::MemoryRegionInfo> items_;
};

class HandlesTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColumnHandle = 0, ColumnType, ColumnName, ColumnCount };
    explicit HandlesTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setData(std::vector<core::HandleInfo> items);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::HandleInfo> items_;
};

class NetworkTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColumnProtocol = 0,
        ColumnLocalAddress,
        ColumnLocalPort,
        ColumnRemoteAddress,
        ColumnRemotePort,
        ColumnState,
        ColumnCount,
    };
    explicit NetworkTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    void setData(std::vector<core::NetworkConnectionInfo> items);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::NetworkConnectionInfo> items_;
};

} // namespace gui
