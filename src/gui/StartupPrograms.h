#pragma once

#include <QList>
#include <QString>

namespace gui {

struct StartupEntry {
    QString name;
    QString command;
    QString source; // .desktop file path (Linux) or registry key (Windows)
};

// Best-effort, read-only enumeration of "runs at login" entries - Linux:
// *.desktop files in ~/.config/autostart and /etc/xdg/autostart; Windows:
// the HKCU/HKLM ...\CurrentVersion\Run registry keys. No elevation, no
// editing - this only reports what's already configured, the same
// information Process Hacker's own Startup tab (or Windows' Task
// Manager/Autoruns) surfaces.
QList<StartupEntry> enumerateStartupPrograms();

} // namespace gui
