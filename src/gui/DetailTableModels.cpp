#include "DetailTableModels.h"

#include <QLocale>

namespace gui {

namespace {

QString hexAddress(uint64_t value) {
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

QString protocolToString(core::NetworkProtocol protocol) {
    switch (protocol) {
        case core::NetworkProtocol::Tcp: return QStringLiteral("TCP");
        case core::NetworkProtocol::Udp: return QStringLiteral("UDP");
        case core::NetworkProtocol::Tcp6: return QStringLiteral("TCPv6");
        case core::NetworkProtocol::Udp6: return QStringLiteral("UDPv6");
        default: return QStringLiteral("?");
    }
}

} // namespace

// --- ThreadsTableModel -------------------------------------------------

void ThreadsTableModel::setData(std::vector<core::ThreadInfo> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

int ThreadsTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int ThreadsTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ThreadsTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnTid: return static_cast<qulonglong>(item.tid);
        case ColumnState: return QString::fromStdString(item.state);
        case ColumnPriority: return item.priority;
        default: return {};
    }
}

QVariant ThreadsTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnTid: return QStringLiteral("TID");
        case ColumnState: return QStringLiteral("State");
        case ColumnPriority: return QStringLiteral("Priority");
        default: return {};
    }
}

// --- ModulesTableModel ---------------------------------------------------

void ModulesTableModel::setData(std::vector<core::ModuleInfo> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

int ModulesTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int ModulesTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ModulesTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnName: return QString::fromStdString(item.name);
        case ColumnBase: return hexAddress(item.baseAddress);
        case ColumnSize: return QLocale().formattedDataSize(static_cast<qint64>(item.sizeBytes));
        case ColumnPath: return QString::fromStdString(item.path);
        default: return {};
    }
}

QVariant ModulesTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnName: return QStringLiteral("Name");
        case ColumnBase: return QStringLiteral("Base");
        case ColumnSize: return QStringLiteral("Size");
        case ColumnPath: return QStringLiteral("Path");
        default: return {};
    }
}

// --- MemoryRegionsTableModel ----------------------------------------------

void MemoryRegionsTableModel::setData(std::vector<core::MemoryRegionInfo> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

int MemoryRegionsTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int MemoryRegionsTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MemoryRegionsTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnBase: return hexAddress(item.baseAddress);
        case ColumnSize: return QLocale().formattedDataSize(static_cast<qint64>(item.sizeBytes));
        case ColumnProtection: return QString::fromStdString(item.protection);
        case ColumnMappedFile: return QString::fromStdString(item.mappedFile);
        default: return {};
    }
}

QVariant MemoryRegionsTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnBase: return QStringLiteral("Base");
        case ColumnSize: return QStringLiteral("Size");
        case ColumnProtection: return QStringLiteral("Protection");
        case ColumnMappedFile: return QStringLiteral("Mapped file");
        default: return {};
    }
}

// --- HandlesTableModel ---------------------------------------------------

void HandlesTableModel::setData(std::vector<core::HandleInfo> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

int HandlesTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int HandlesTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HandlesTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnHandle: return static_cast<qulonglong>(item.handleValueOrFd);
        case ColumnType: return QString::fromStdString(item.type);
        case ColumnName: return QString::fromStdString(item.name);
        default: return {};
    }
}

QVariant HandlesTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnHandle: return QStringLiteral("Handle");
        case ColumnType: return QStringLiteral("Type");
        case ColumnName: return QStringLiteral("Name / target");
        default: return {};
    }
}

// --- NetworkTableModel ---------------------------------------------------

void NetworkTableModel::setData(std::vector<core::NetworkConnectionInfo> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

int NetworkTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int NetworkTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant NetworkTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= items_.size()) {
        return {};
    }
    const auto &item = items_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnProtocol: return protocolToString(item.protocol);
        case ColumnLocalAddress: return QString::fromStdString(item.localAddress);
        case ColumnLocalPort: return item.localPort;
        case ColumnRemoteAddress: return QString::fromStdString(item.remoteAddress);
        case ColumnRemotePort: return item.remotePort;
        case ColumnState: return QString::fromStdString(item.state);
        default: return {};
    }
}

QVariant NetworkTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnProtocol: return QStringLiteral("Proto");
        case ColumnLocalAddress: return QStringLiteral("Local address");
        case ColumnLocalPort: return QStringLiteral("Local port");
        case ColumnRemoteAddress: return QStringLiteral("Remote address");
        case ColumnRemotePort: return QStringLiteral("Remote port");
        case ColumnState: return QStringLiteral("State");
        default: return {};
    }
}

} // namespace gui
