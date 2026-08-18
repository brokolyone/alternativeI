#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "../core/ProcessInfo.h"

namespace gui {

class ProcessTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ColumnName = 0,
        ColumnPid,
        ColumnPpid,
        ColumnCpu,
        ColumnMemory,
        ColumnThreads,
        ColumnUser,
        ColumnPriority,
        ColumnPath,
        ColumnCount,
    };

    explicit ProcessTableModel(QObject *parent = nullptr);

    void setProcesses(std::vector<core::ProcessInfo> processes);
    const core::ProcessInfo *processAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::ProcessInfo> processes_;
};

} // namespace gui
