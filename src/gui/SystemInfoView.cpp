#include "SystemInfoView.h"

#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QSysInfo>
#include <thread>

#include "i18n.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#endif

namespace gui {

namespace {

// Seconds since boot. Windows: GetTickCount64() (uptime of the *machine*,
// not just this process). Linux: the first field of /proc/uptime.
double systemUptimeSeconds() {
#if defined(_WIN32)
    return static_cast<double>(GetTickCount64()) / 1000.0;
#else
    if (FILE *file = std::fopen("/proc/uptime", "r")) {
        double uptime = 0.0;
        const int matched = std::fscanf(file, "%lf", &uptime);
        std::fclose(file);
        if (matched == 1) {
            return uptime;
        }
    }
    return 0.0;
#endif
}

QString formatUptime(double seconds) {
    const qint64 totalSeconds = static_cast<qint64>(seconds);
    const qint64 days = totalSeconds / 86400;
    const qint64 hours = (totalSeconds % 86400) / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    if (days > 0) {
        return i18n::t("%1d %2h %3m", "%1д %2ч %3м").arg(days).arg(hours).arg(minutes);
    }
    if (hours > 0) {
        return i18n::t("%1h %2m", "%1ч %2м").arg(hours).arg(minutes);
    }
    return i18n::t("%1m", "%1м").arg(minutes);
}

QLabel *addRow(QFormLayout *form, const QString &label, const QString &value, QWidget *parent) {
    auto *valueLabel = new QLabel(value, parent);
    form->addRow(label, valueLabel);
    return valueLabel;
}

} // namespace

SystemInfoView::SystemInfoView(QWidget *parent)
    : QWidget(parent), monitor_(core::createDefaultSystemMonitor()) {
    auto *form = new QFormLayout(this);

    unsigned int coreCount = std::thread::hardware_concurrency();
    if (coreCount == 0) {
        coreCount = 1;
    }

    addRow(form, i18n::t("Operating system:", "Операционная система:"), QSysInfo::prettyProductName(),
           this);
    addRow(form, i18n::t("Kernel:", "Ядро:"),
           QStringLiteral("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion()), this);
    addRow(form, i18n::t("Hostname:", "Имя хоста:"), QSysInfo::machineHostName(), this);
    addRow(form, i18n::t("Architecture:", "Архитектура:"), QSysInfo::currentCpuArchitecture(), this);
    addRow(form, i18n::t("CPU cores:", "Ядер CPU:"), QString::number(coreCount), this);

    cpuLabel_ = addRow(form, i18n::t("CPU load:", "Загрузка CPU:"), QString(), this);
    memoryLabel_ = addRow(form, i18n::t("Memory:", "Память:"), QString(), this);
    uptimeLabel_ = addRow(form, i18n::t("Uptime:", "Время работы:"), QString(), this);

    connect(&timer_, &QTimer::timeout, this, &SystemInfoView::refresh);
    timer_.start(1000);
    refresh();
}

void SystemInfoView::refresh() {
    const core::SystemStats stats = monitor_->sample();
    cpuLabel_->setText(QStringLiteral("%1%").arg(QString::number(stats.cpuPercent, 'f', 1)));
    memoryLabel_->setText(i18n::t("%1 / %2", "%1 / %2")
                               .arg(QLocale().formattedDataSize(static_cast<qint64>(stats.memoryUsedBytes)))
                               .arg(QLocale().formattedDataSize(static_cast<qint64>(stats.memoryTotalBytes))));
    uptimeLabel_->setText(formatUptime(systemUptimeSeconds()));
}

} // namespace gui
