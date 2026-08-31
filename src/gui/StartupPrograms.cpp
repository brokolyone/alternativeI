#include "StartupPrograms.h"

#include <QSettings>

#if defined(__linux__)
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#endif

namespace gui {

#if defined(_WIN32)

namespace {
void collectFromRegistryRun(const QString &hive, QList<StartupEntry> *out) {
    QSettings reg(hive, QSettings::NativeFormat);
    const QStringList keys = reg.allKeys();
    for (const QString &key : keys) {
        StartupEntry entry;
        entry.name = key;
        entry.command = reg.value(key).toString();
        entry.source = hive;
        out->push_back(entry);
    }
}
} // namespace

QList<StartupEntry> enumerateStartupPrograms() {
    QList<StartupEntry> result;
    collectFromRegistryRun(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), &result);
    collectFromRegistryRun(
        QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), &result);
    return result;
}

#elif defined(__linux__)

namespace {

// Minimal .desktop parser: only the handful of keys this view needs, from
// the unlocalized [Desktop Entry] group - not a full freedesktop.org
// Desktop Entry Specification implementation.
void parseDesktopFile(const QString &path, QList<StartupEntry> *out) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    QString name;
    QString exec;
    bool hidden = false;
    bool inDesktopEntryGroup = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntryGroup = (line == QStringLiteral("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntryGroup) {
            continue;
        }
        if (line.startsWith(QStringLiteral("Name=")) && name.isEmpty()) {
            name = line.mid(5);
        } else if (line.startsWith(QStringLiteral("Exec="))) {
            exec = line.mid(5);
        } else if (line == QStringLiteral("Hidden=true") ||
                   line == QStringLiteral("X-GNOME-Autostart-enabled=false")) {
            hidden = true;
        }
    }

    if (hidden || exec.isEmpty()) {
        return;
    }
    StartupEntry entry;
    entry.name = name.isEmpty() ? QFileInfo(path).baseName() : name;
    entry.command = exec;
    entry.source = path;
    out->push_back(entry);
}

void scanAutostartDir(const QString &dirPath, QList<StartupEntry> *out) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.desktop"), QDir::Files);
    for (const QString &fileName : files) {
        parseDesktopFile(dir.filePath(fileName), out);
    }
}

} // namespace

QList<StartupEntry> enumerateStartupPrograms() {
    QList<StartupEntry> result;
    scanAutostartDir(QDir::homePath() + QStringLiteral("/.config/autostart"), &result);
    scanAutostartDir(QStringLiteral("/etc/xdg/autostart"), &result);
    return result;
}

#else

QList<StartupEntry> enumerateStartupPrograms() {
    return {};
}

#endif

} // namespace gui
