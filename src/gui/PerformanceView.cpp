#include "PerformanceView.h"

#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include "SparklineWidget.h"
#include "i18n.h"

namespace gui {

namespace {

QString formatBytesPerSec(double bytesPerSec) {
    return QLocale().formattedDataSize(static_cast<qint64>(bytesPerSec)) + "/s";
}

} // namespace

PerformanceView::PerformanceView(QWidget *parent)
    : QWidget(parent), monitor_(core::createDefaultSystemMonitor()) {
    auto *grid = new QGridLayout(this);

    cpuGraph_ = addGraph(grid, 0, 0, i18n::t("CPU", "ЦП"), QColor(220, 90, 90), 100.0);
    memoryGraph_ = addGraph(grid, 0, 1, i18n::t("Memory", "Память"), QColor(90, 160, 220), 100.0);
    diskGraph_ = addGraph(grid, 1, 0, i18n::t("Disk I/O", "Диск (I/O)"), QColor(90, 200, 130), -1.0);
    networkGraph_ = addGraph(grid, 1, 1, i18n::t("Network", "Сеть"), QColor(200, 170, 90), -1.0);

    connect(&timer_, &QTimer::timeout, this, &PerformanceView::sample);
    timer_.start(1000);
    sample();
}

PerformanceView::Graph PerformanceView::addGraph(QGridLayout *grid, int row, int col,
                                                  const QString &title, const QColor &color,
                                                  double fixedMax) {
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    Graph graph;
    graph.titleLabel = new QLabel(title, container);
    QFont boldFont = graph.titleLabel->font();
    boldFont.setBold(true);
    graph.titleLabel->setFont(boldFont);

    graph.sparkline = new SparklineWidget(container);
    graph.sparkline->setColor(color);
    if (fixedMax >= 0.0) {
        graph.sparkline->setFixedMaximum(fixedMax);
    }

    layout->addWidget(graph.titleLabel);
    layout->addWidget(graph.sparkline);
    grid->addWidget(container, row, col);

    return graph;
}

void PerformanceView::sample() {
    const core::SystemStats stats = monitor_->sample();

    cpuGraph_.sparkline->addSample(stats.cpuPercent);
    cpuGraph_.titleLabel->setText(
        i18n::t("CPU: %1%", "ЦП: %1%").arg(QString::number(stats.cpuPercent, 'f', 1)));

    const double memPercent = stats.memoryTotalBytes > 0
        ? 100.0 * static_cast<double>(stats.memoryUsedBytes) / static_cast<double>(stats.memoryTotalBytes)
        : 0.0;
    memoryGraph_.sparkline->addSample(memPercent);
    memoryGraph_.titleLabel->setText(i18n::t("Memory: %1 / %2 (%3%)", "Память: %1 / %2 (%3%)")
                                          .arg(QLocale().formattedDataSize(
                                              static_cast<qint64>(stats.memoryUsedBytes)))
                                          .arg(QLocale().formattedDataSize(
                                              static_cast<qint64>(stats.memoryTotalBytes)))
                                          .arg(QString::number(memPercent, 'f', 1)));

    diskGraph_.sparkline->addSample(stats.diskReadBytesPerSec + stats.diskWriteBytesPerSec);
    diskGraph_.titleLabel->setText(i18n::t("Disk I/O: R %1  W %2", "Диск (I/O): Ч %1  З %2")
                                        .arg(formatBytesPerSec(stats.diskReadBytesPerSec))
                                        .arg(formatBytesPerSec(stats.diskWriteBytesPerSec)));

    networkGraph_.sparkline->addSample(stats.netRecvBytesPerSec + stats.netSentBytesPerSec);
    networkGraph_.titleLabel->setText(i18n::t("Network: ↓ %1  ↑ %2", "Сеть: ↓ %1  ↑ %2")
                                           .arg(formatBytesPerSec(stats.netRecvBytesPerSec))
                                           .arg(formatBytesPerSec(stats.netSentBytesPerSec)));
}

} // namespace gui
