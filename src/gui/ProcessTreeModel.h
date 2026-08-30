#pragma once

#include <QAbstractItemModel>
#include <QSet>
#include <memory>
#include <vector>

#include "../core/ProcessInfo.h"

namespace gui {

// Tree model that groups processes by parent PID, like Process Hacker's
// default process view. Orphans (parent not present in the current
// snapshot, e.g. PID 0/init's own parent) are attached to a synthetic
// root so every process is still reachable.
class ProcessTreeModel : public QAbstractItemModel {
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

    explicit ProcessTreeModel(QObject *parent = nullptr);
    ~ProcessTreeModel() override;

    void setProcesses(std::vector<core::ProcessInfo> processes);
    const core::ProcessInfo *processForIndex(const QModelIndex &index) const;

    // Finds the index of the row showing this PID after a rebuild, so the
    // view can restore expansion/selection state across refreshes.
    QModelIndex indexForPid(uint64_t pid) const;

    // All PIDs below this one in the tree (children, grandchildren, ...),
    // for "terminate process tree" - excludes pid itself.
    std::vector<uint64_t> descendantPids(uint64_t pid) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    struct Node {
        core::ProcessInfo info;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeFromIndex(const QModelIndex &index) const;
    bool findPid(Node *node, uint64_t pid, QModelIndex ancestorIndex, QModelIndex *outIndex) const;
    void collectDescendants(Node *node, std::vector<uint64_t> *out) const;

    std::unique_ptr<Node> root_;
};

} // namespace gui
