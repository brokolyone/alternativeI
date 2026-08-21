#include "DetailTableModels.h"

#include <QLocale>

#include "i18n.h"

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
        case ColumnTid: return i18n::t("TID", "TID");
        case ColumnState: return i18n::t("State", "Состояние");
        case ColumnPriority: return i18n::t("Priority", "Приоритет");
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
        case ColumnName: return i18n::t("Name", "Имя");
        case ColumnBase: return i18n::t("Base", "База");
        case ColumnSize: return i18n::t("Size", "Размер");
        case ColumnPath: return i18n::t("Path", "Путь");
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
        case ColumnBase: return i18n::t("Base", "База");
        case ColumnSize: return i18n::t("Size", "Размер");
        case ColumnProtection: return i18n::t("Protection", "Защита");
        case ColumnMappedFile: return i18n::t("Mapped file", "Отображённый файл");
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
        case ColumnHandle: return i18n::t("Handle", "Хендл");
        case ColumnType: return i18n::t("Type", "Тип");
        case ColumnName: return i18n::t("Name / target", "Имя / цель");
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
        case ColumnProtocol: return i18n::t("Proto", "Протокол");
        case ColumnLocalAddress: return i18n::t("Local address", "Локальный адрес");
        case ColumnLocalPort: return i18n::t("Local port", "Локальный порт");
        case ColumnRemoteAddress: return i18n::t("Remote address", "Удалённый адрес");
        case ColumnRemotePort: return i18n::t("Remote port", "Удалённый порт");
        case ColumnState: return i18n::t("State", "Состояние");
        default: return {};
    }
}

} // namespace gui
