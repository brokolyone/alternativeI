#pragma once

#include <QMainWindow>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <memory>

#include "../core/IProcessProvider.h"
#include "ProcessTreeModel.h"

class QLineEdit;
class QTreeView;
class QLabel;

namespace gui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void refresh();
    void showProcessContextMenu(const QPoint &pos);
    void terminateSelected();
    void terminateSelectedTrees();
    void suspendSelected();
    void resumeSelected();
    void setAffinityForSelected();
    void openFileLocationForSelected();
    void copyProcessInfoForSelected();
    void setPrioritySelected(core::ProcessPriority priority);
    void onRowExpanded(const QModelIndex &proxyIndex);
    void onRowCollapsed(const QModelIndex &proxyIndex);
    void openPropertiesForSelected();
    void openPropertiesForIndex(const QModelIndex &proxyIndex);
    void showAboutDialog();
    void showSettingsDialog();

private:
    void buildUi();
    void buildToolbar();
    void restoreTreeState();

    std::unique_ptr<core::IProcessProvider> provider_;
    ProcessTreeModel *model_;
    QSortFilterProxyModel *proxyModel_;
    QTreeView *treeView_;
    QLineEdit *searchBox_;
    QLabel *statusLabel_;
    QTimer refreshTimer_;
    int refreshIntervalMs_ = 1000;

    QSet<uint64_t> expandedPids_;
    bool autoExpandDone_ = false;
};

} // namespace gui
