#pragma once

#include <QWidget>

class QStandardItemModel;
class QTableView;

namespace gui {

// "Startup" tab: read-only list of what's configured to run at login
// (see StartupPrograms.h). No enable/disable/remove yet - surfacing the
// list itself is the useful part for now.
class StartupProgramsView : public QWidget {
    Q_OBJECT

public:
    explicit StartupProgramsView(QWidget *parent = nullptr);

private slots:
    void reload();

private:
    QStandardItemModel *model_;
    QTableView *tableView_;
};

} // namespace gui
