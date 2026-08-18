#include "ProcessTreeModel.h"

#include <QLocale>
#include <unordered_map>

namespace gui {

namespace {

QString priorityToString(core::ProcessPriority priority) {
    switch (priority) {
        case core::ProcessPriority::Idle: return QStringLiteral("Idle");
        case core::ProcessPriority::BelowNormal: return QStringLiteral("Below normal");
        case core::ProcessPriority::Normal: return QStringLiteral("Normal");
        case core::ProcessPriority::AboveNormal: return QStringLiteral("Above normal");
        case core::ProcessPriority::High: return QStringLiteral("High");
        case core::ProcessPriority::Realtime: return QStringLiteral("Realtime");
        default: return QStringLiteral("Unknown");
    }
}

} // namespace

ProcessTreeModel::ProcessTreeModel(QObject *parent) : QAbstractItemModel(parent) {
    root_ = std::make_unique<Node>();
}

ProcessTreeModel::~ProcessTreeModel() = default;

void ProcessTreeModel::setProcesses(std::vector<core::ProcessInfo> processes) {
    beginResetModel();

    std::unordered_map<uint64_t, std::unique_ptr<Node>> nodesByPid;
    nodesByPid.reserve(processes.size());
    for (auto &info : processes) {
        auto node = std::make_unique<Node>();
        const uint64_t pid = info.pid;
        node->info = std::move(info);
        nodesByPid.emplace(pid, std::move(node));
    }

    // Raw lookup that stays valid for the whole pass below: Node addresses
    // never change when a unique_ptr<Node> is moved between containers,
    // only ownership does, so this map can outlive individual erase()s on
    // nodesByPid without dangling.
    std::unordered_map<uint64_t, Node *> rawByPid;
    rawByPid.reserve(nodesByPid.size());
    for (auto &kv : nodesByPid) {
        rawByPid[kv.first] = kv.second.get();
    }

    root_ = std::make_unique<Node>();

    std::vector<uint64_t> pids;
    pids.reserve(nodesByPid.size());
    for (auto &kv : nodesByPid) {
        pids.push_back(kv.first);
    }

    for (uint64_t pid : pids) {
        auto it = nodesByPid.find(pid);
        if (it == nodesByPid.end()) {
            continue;
        }
        std::unique_ptr<Node> node = std::move(it->second);
        nodesByPid.erase(it);

        Node *parentNode = root_.get();
        const uint64_t ppid = node->info.ppid;
        if (ppid != pid) {
            auto parentIt = rawByPid.find(ppid);
            if (parentIt != rawByPid.end()) {
                parentNode = parentIt->second;
            }
        }

        node->parent = parentNode;
        parentNode->children.push_back(std::move(node));
    }

    endResetModel();
}

ProcessTreeModel::Node *ProcessTreeModel::nodeFromIndex(const QModelIndex &index) const {
    if (!index.isValid()) {
        return root_.get();
    }
    return static_cast<Node *>(index.internalPointer());
}

const core::ProcessInfo *ProcessTreeModel::processForIndex(const QModelIndex &index) const {
    Node *node = nodeFromIndex(index);
    if (node == nullptr || node == root_.get()) {
        return nullptr;
    }
    return &node->info;
}

bool ProcessTreeModel::findPid(Node *node, uint64_t pid, QModelIndex ancestorIndex,
                                QModelIndex *outIndex) const {
    for (size_t row = 0; row < node->children.size(); ++row) {
        Node *child = node->children[row].get();
        const QModelIndex childIndex = index(static_cast<int>(row), 0, ancestorIndex);
        if (child->info.pid == pid) {
            *outIndex = childIndex;
            return true;
        }
        if (findPid(child, pid, childIndex, outIndex)) {
            return true;
        }
    }
    return false;
}

QModelIndex ProcessTreeModel::indexForPid(uint64_t pid) const {
    QModelIndex result;
    if (root_ && findPid(root_.get(), pid, QModelIndex(), &result)) {
        return result;
    }
    return QModelIndex();
}

QModelIndex ProcessTreeModel::index(int row, int column, const QModelIndex &parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    Node *parentNode = nodeFromIndex(parent);
    if (parentNode == nullptr || static_cast<size_t>(row) >= parentNode->children.size()) {
        return QModelIndex();
    }
    return createIndex(row, column, parentNode->children[static_cast<size_t>(row)].get());
}

QModelIndex ProcessTreeModel::parent(const QModelIndex &child) const {
    if (!child.isValid()) {
        return QModelIndex();
    }
    Node *node = nodeFromIndex(child);
    Node *parentNode = node->parent;
    if (parentNode == nullptr || parentNode == root_.get()) {
        return QModelIndex();
    }

    Node *grandparent = parentNode->parent;
    if (grandparent == nullptr) {
        return QModelIndex();
    }

    for (size_t row = 0; row < grandparent->children.size(); ++row) {
        if (grandparent->children[row].get() == parentNode) {
            return createIndex(static_cast<int>(row), 0, parentNode);
        }
    }
    return QModelIndex();
}

int ProcessTreeModel::rowCount(const QModelIndex &parent) const {
    if (parent.column() > 0) {
        return 0;
    }
    Node *parentNode = nodeFromIndex(parent);
    return parentNode ? static_cast<int>(parentNode->children.size()) : 0;
}

int ProcessTreeModel::columnCount(const QModelIndex & /*parent*/) const {
    return ColumnCount;
}

QVariant ProcessTreeModel::data(const QModelIndex &index, int role) const {
    const core::ProcessInfo *proc = processForIndex(index);
    if (proc == nullptr) {
        return {};
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case ColumnPid:
            case ColumnPpid:
            case ColumnCpu:
            case ColumnMemory:
            case ColumnThreads:
                return QVariant(Qt::AlignRight | Qt::AlignVCenter);
            default:
                return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    if (role != Qt::DisplayRole) {
        return {};
    }

    switch (index.column()) {
        case ColumnName: return QString::fromStdString(proc->name);
        case ColumnPid: return static_cast<qulonglong>(proc->pid);
        case ColumnPpid: return static_cast<qulonglong>(proc->ppid);
        case ColumnCpu: return QString::number(proc->cpuPercent, 'f', 1) + "%";
        case ColumnMemory: return QLocale().formattedDataSize(static_cast<qint64>(proc->privateBytes));
        case ColumnThreads: return static_cast<qulonglong>(proc->threadCount);
        case ColumnUser: return QString::fromStdString(proc->user);
        case ColumnPriority: return priorityToString(proc->priority);
        case ColumnPath: return QString::fromStdString(proc->exePath);
        default: return {};
    }
}

QVariant ProcessTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractItemModel::headerData(section, orientation, role);
    }

    switch (section) {
        case ColumnName: return QStringLiteral("Name");
        case ColumnPid: return QStringLiteral("PID");
        case ColumnPpid: return QStringLiteral("PPID");
        case ColumnCpu: return QStringLiteral("CPU");
        case ColumnMemory: return QStringLiteral("Memory");
        case ColumnThreads: return QStringLiteral("Threads");
        case ColumnUser: return QStringLiteral("User");
        case ColumnPriority: return QStringLiteral("Priority");
        case ColumnPath: return QStringLiteral("Path");
        default: return {};
    }
}

} // namespace gui
