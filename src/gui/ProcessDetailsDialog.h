#pragma once

#include <QDialog>
#include <QTimer>

#include "../core/IProcessProvider.h"
#include "DetailTableModels.h"

class QPlainTextEdit;

namespace gui {

// Properties dialog for a single process: threads, modules, memory
// regions, handles, environment and network tabs. Data is pulled from
// IProcessProvider on open and refreshed on a timer while visible - these
// per-process queries are more expensive than the main list snapshot, so
// they're only gathered while someone is actually looking at this process.
class ProcessDetailsDialog : public QDialog {
    Q_OBJECT

public:
    ProcessDetailsDialog(core::IProcessProvider *provider, uint64_t pid, const QString &processName,
                          QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    core::IProcessProvider *provider_;
    uint64_t pid_;

    ThreadsTableModel *threadsModel_;
    ModulesTableModel *modulesModel_;
    MemoryRegionsTableModel *memoryModel_;
    HandlesTableModel *handlesModel_;
    NetworkTableModel *networkModel_;
    QPlainTextEdit *environmentView_;

    QTimer refreshTimer_;
};

} // namespace gui
