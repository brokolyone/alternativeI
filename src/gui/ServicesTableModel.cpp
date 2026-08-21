#include "ServicesTableModel.h"

#include "i18n.h"

namespace gui {

void ServicesTableModel::setServices(std::vector<core::ServiceInfo> services) {
    beginResetModel();
    services_ = std::move(services);
    endResetModel();
}

const core::ServiceInfo *ServicesTableModel::serviceAt(int row) const {
    if (row < 0 || static_cast<size_t>(row) >= services_.size()) {
        return nullptr;
    }
    return &services_[static_cast<size_t>(row)];
}

int ServicesTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(services_.size());
}

int ServicesTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ServicesTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= services_.size()) {
        return {};
    }
    const auto &service = services_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnName: return QString::fromStdString(service.name);
        case ColumnDisplayName: return QString::fromStdString(service.displayName);
        case ColumnState: return QString::fromStdString(service.state);
        case ColumnStartType: return QString::fromStdString(service.startType);
        default: return {};
    }
}

QVariant ServicesTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnName: return i18n::t("Name", "Имя");
        case ColumnDisplayName: return i18n::t("Description", "Описание");
        case ColumnState: return i18n::t("State", "Состояние");
        case ColumnStartType: return i18n::t("Start type", "Тип запуска");
        default: return {};
    }
}

} // namespace gui
