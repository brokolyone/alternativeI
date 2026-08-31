#pragma once

#include <QTimer>
#include <QWidget>
#include <memory>

#include "../core/ISystemMonitor.h"

class QLabel;

namespace gui {

// "System" tab: static machine facts (OS, kernel, hostname, architecture,
// core count, total RAM) plus a few numbers that change over time (CPU
// load, memory used, uptime), refreshed once a second.
class SystemInfoView : public QWidget {
    Q_OBJECT

public:
    explicit SystemInfoView(QWidget *parent = nullptr);

private slots:
    void refresh();

private:
    std::unique_ptr<core::ISystemMonitor> monitor_;
    QLabel *cpuLabel_;
    QLabel *memoryLabel_;
    QLabel *uptimeLabel_;
    QTimer timer_;
};

} // namespace gui
