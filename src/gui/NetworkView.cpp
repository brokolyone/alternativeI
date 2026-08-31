#include "NetworkView.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include "i18n.h"

namespace gui {

namespace {
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

SystemNetworkTableModel::SystemNetworkTableModel(QObject *parent) : QAbstractTableModel(parent) {}

void SystemNetworkTableModel::setEntries(std::vector<Entry> entries) {
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}

int SystemNetworkTableModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

int SystemNetworkTableModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SystemNetworkTableModel::data(const QModelIndex &index, int role) const {
    if (role != Qt::DisplayRole || !index.isValid() ||
        static_cast<size_t>(index.row()) >= entries_.size()) {
        return {};
    }
    const Entry &entry = entries_[static_cast<size_t>(index.row())];
    switch (index.column()) {
        case ColumnProcess: return entry.processName;
        case ColumnPid: return static_cast<qulonglong>(entry.pid);
        case ColumnProtocol: return protocolToString(entry.connection.protocol);
        case ColumnLocalAddress: return QString::fromStdString(entry.connection.localAddress);
        case ColumnLocalPort: return entry.connection.localPort;
        case ColumnRemoteAddress: return QString::fromStdString(entry.connection.remoteAddress);
        case ColumnRemotePort: return entry.connection.remotePort;
        case ColumnState: return QString::fromStdString(entry.connection.state);
        default: return {};
    }
}

QVariant SystemNetworkTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case ColumnProcess: return i18n::t("Process", "Процесс");
        case ColumnPid: return i18n::t("PID", "PID");
        case ColumnProtocol: return i18n::t("Proto", "Протокол");
        case ColumnLocalAddress: return i18n::t("Local address", "Локальный адрес");
        case ColumnLocalPort: return i18n::t("Local port", "Локальный порт");
        case ColumnRemoteAddress: return i18n::t("Remote address", "Удалённый адрес");
        case ColumnRemotePort: return i18n::t("Remote port", "Удалённый порт");
        case ColumnState: return i18n::t("State", "Состояние");
        default: return {};
    }
}

NetworkView::NetworkView(QWidget *parent)
    : QWidget(parent), provider_(core::createDefaultProcessProvider()) {
    auto *layout = new QVBoxLayout(this);

    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText(i18n::t("Filter connections...", "Фильтр соединений..."));
    layout->addWidget(searchBox_);

    model_ = new SystemNetworkTableModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setFilterKeyColumn(-1);
    connect(searchBox_, &QLineEdit::textChanged, proxyModel_,
            &QSortFilterProxyModel::setFilterFixedString);

    tableView_ = new QTableView(this);
    tableView_->setModel(proxyModel_);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSortingEnabled(true);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->verticalHeader()->setVisible(false);
    layout->addWidget(tableView_);

    // A slower interval than the Processes tab's default 1s: this walks
    // every visible process' connections each tick (snapshot() + N calls
    // to networkConnections()), noticeably heavier than one snapshot().
    connect(&refreshTimer_, &QTimer::timeout, this, &NetworkView::refresh);
    refreshTimer_.start(3000);
    refresh();
}

void NetworkView::refresh() {
    std::vector<SystemNetworkTableModel::Entry> entries;
    for (const core::ProcessInfo &proc : provider_->snapshot()) {
        for (const core::NetworkConnectionInfo &conn : provider_->networkConnections(proc.pid)) {
            SystemNetworkTableModel::Entry entry;
            entry.processName = QString::fromStdString(proc.name);
            entry.pid = proc.pid;
            entry.connection = conn;
            entries.push_back(std::move(entry));
        }
    }
    model_->setEntries(std::move(entries));
}

} // namespace gui
