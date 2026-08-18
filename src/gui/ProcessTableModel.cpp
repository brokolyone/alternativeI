#include "ProcessTableModel.h"

#include <QLocale>

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

ProcessTableModel::ProcessTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void ProcessTableModel::setProcesses(std::vector<core::ProcessInfo> processes) {
    beginResetModel();
    processes_ = std::move(processes);
    endResetModel();
}

const core::ProcessInfo *ProcessTableModel::processAt(int row) const {
    if (row < 0 || static_cast<size_t>(row) >= processes_.size()) {
        return nullptr;
    }
    return &processes_[static_cast<size_t>(row)];
}

int ProcessTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(processes_.size());
}

int ProcessTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant ProcessTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || static_cast<size_t>(index.row()) >= processes_.size()) {
        return {};
    }

    const auto &proc = processes_[static_cast<size_t>(index.row())];

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
        case ColumnName: return QString::fromStdString(proc.name);
        case ColumnPid: return static_cast<qulonglong>(proc.pid);
        case ColumnPpid: return static_cast<qulonglong>(proc.ppid);
        case ColumnCpu: return QString::number(proc.cpuPercent, 'f', 1) + "%";
        case ColumnMemory: return QLocale().formattedDataSize(static_cast<qint64>(proc.privateBytes));
        case ColumnThreads: return static_cast<qulonglong>(proc.threadCount);
        case ColumnUser: return QString::fromStdString(proc.user);
        case ColumnPriority: return priorityToString(proc.priority);
        case ColumnPath: return QString::fromStdString(proc.exePath);
        default: return {};
    }
}

QVariant ProcessTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
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
