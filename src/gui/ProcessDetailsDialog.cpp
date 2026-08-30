#include "ProcessDetailsDialog.h"

#include <QHeaderView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

#include "i18n.h"

namespace gui {

namespace {

// Table + a filter box above it (recursive-substring match across every
// column, same QSortFilterProxyModel pattern the Processes/Services lists
// use) so a long Handles/Modules/Network list can be searched instead of
// only sorted.
QWidget *makeFilterableTableView(QAbstractItemModel *model, QWidget *parent) {
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *filterBox = new QLineEdit(container);
    filterBox->setPlaceholderText(i18n::t("Filter...", "Фильтр..."));
    layout->addWidget(filterBox);

    auto *proxy = new QSortFilterProxyModel(container);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(-1);
    QObject::connect(filterBox, &QLineEdit::textChanged, proxy, &QSortFilterProxyModel::setFilterFixedString);

    auto *view = new QTableView(container);
    view->setModel(proxy);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->setVisible(false);
    layout->addWidget(view);

    return container;
}

} // namespace

ProcessDetailsDialog::ProcessDetailsDialog(core::IProcessProvider *provider, uint64_t pid,
                                            const QString &processName, QWidget *parent)
    : QDialog(parent), provider_(provider), pid_(pid) {
    setWindowTitle(
        i18n::t("%1 (PID %2) Properties", "%1 (PID %2) — Свойства").arg(processName).arg(pid));
    resize(800, 550);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    threadsModel_ = new ThreadsTableModel(this);
    tabs->addTab(makeFilterableTableView(threadsModel_, this), i18n::t("Threads", "Потоки"));

    modulesModel_ = new ModulesTableModel(this);
    tabs->addTab(makeFilterableTableView(modulesModel_, this), i18n::t("Modules", "Модули"));

    memoryModel_ = new MemoryRegionsTableModel(this);
    tabs->addTab(makeFilterableTableView(memoryModel_, this), i18n::t("Memory", "Память"));

    handlesModel_ = new HandlesTableModel(this);
    tabs->addTab(makeFilterableTableView(handlesModel_, this), i18n::t("Handles", "Хендлы"));

    networkModel_ = new NetworkTableModel(this);
    tabs->addTab(makeFilterableTableView(networkModel_, this), i18n::t("Network", "Сеть"));

    environmentView_ = new QPlainTextEdit(this);
    environmentView_->setReadOnly(true);
    environmentView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tabs->addTab(environmentView_, i18n::t("Environment", "Окружение"));

    connect(&refreshTimer_, &QTimer::timeout, this, &ProcessDetailsDialog::refresh);
    refreshTimer_.start(2000);
    refresh();
}

void ProcessDetailsDialog::refresh() {
    threadsModel_->setData(provider_->threads(pid_));
    modulesModel_->setData(provider_->modules(pid_));
    memoryModel_->setData(provider_->memoryRegions(pid_));
    handlesModel_->setData(provider_->handles(pid_));
    networkModel_->setData(provider_->networkConnections(pid_));

    auto env = provider_->environment(pid_);
    std::sort(env.begin(), env.end());
    QString text;
    for (const auto &line : env) {
        text += QString::fromStdString(line);
        text += '\n';
    }
    // Avoid clobbering the user's cursor/scroll position on every refresh
    // tick if nothing actually changed.
    if (environmentView_->toPlainText() != text) {
        environmentView_->setPlainText(text);
    }
}

} // namespace gui
