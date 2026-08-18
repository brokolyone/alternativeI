#pragma once

#include <QTimer>
#include <QWidget>
#include <memory>

#include "../core/ISystemMonitor.h"

class QLabel;
class QGridLayout;

namespace gui {

class SparklineWidget;

// "Performance" tab: four rolling graphs (CPU, memory, disk I/O, network),
// each with a current-value label, sampled from ISystemMonitor once a
// second - the same cadence as the process list, but on its own timer so
// this tab keeps updating even while the Processes tab is idle/hidden.
class PerformanceView : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceView(QWidget *parent = nullptr);

private slots:
    void sample();

private:
    struct Graph {
        QLabel *titleLabel = nullptr;
        SparklineWidget *sparkline = nullptr;
    };

    Graph addGraph(QGridLayout *grid, int row, int col, const QString &title, const QColor &color,
                   double fixedMax);

    std::unique_ptr<core::ISystemMonitor> monitor_;
    QTimer timer_;

    Graph cpuGraph_;
    Graph memoryGraph_;
    Graph diskGraph_;
    Graph networkGraph_;
};

} // namespace gui
