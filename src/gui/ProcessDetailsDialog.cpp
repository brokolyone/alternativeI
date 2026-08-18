#include "ProcessDetailsDialog.h"

#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <algorithm>

namespace gui {

namespace {

QTableView *makeTableView(QAbstractItemModel *model, QWidget *parent) {
    auto *view = new QTableView(parent);
    view->setModel(model);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->horizontalHeader()->setStretchLastSection(true);
    view->verticalHeader()->setVisible(false);
    return view;
}

} // namespace

ProcessDetailsDialog::ProcessDetailsDialog(core::IProcessProvider *provider, uint64_t pid,
                                            const QString &processName, QWidget *parent)
    : QDialog(parent), provider_(provider), pid_(pid) {
    setWindowTitle(QStringLiteral("%1 (PID %2) Properties").arg(processName).arg(pid));
    resize(800, 550);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    threadsModel_ = new ThreadsTableModel(this);
    tabs->addTab(makeTableView(threadsModel_, this), QStringLiteral("Threads"));

    modulesModel_ = new ModulesTableModel(this);
    tabs->addTab(makeTableView(modulesModel_, this), QStringLiteral("Modules"));

    memoryModel_ = new MemoryRegionsTableModel(this);
    tabs->addTab(makeTableView(memoryModel_, this), QStringLiteral("Memory"));

    handlesModel_ = new HandlesTableModel(this);
    tabs->addTab(makeTableView(handlesModel_, this), QStringLiteral("Handles"));

    networkModel_ = new NetworkTableModel(this);
    tabs->addTab(makeTableView(networkModel_, this), QStringLiteral("Network"));

    environmentView_ = new QPlainTextEdit(this);
    environmentView_->setReadOnly(true);
    environmentView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    tabs->addTab(environmentView_, QStringLiteral("Environment"));

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
