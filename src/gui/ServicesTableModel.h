#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "../core/ServiceInfo.h"

namespace gui {

class ServicesTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColumnName = 0, ColumnDisplayName, ColumnState, ColumnStartType, ColumnCount };

    explicit ServicesTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    void setServices(std::vector<core::ServiceInfo> services);
    const core::ServiceInfo *serviceAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    std::vector<core::ServiceInfo> services_;
};

} // namespace gui
