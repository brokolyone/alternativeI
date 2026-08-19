#pragma once

#include <QList>
#include <QString>

namespace gui {

struct DiskDeviceEntry {
    QString path;
    quint64 sizeBytes = 0;
};

// Best-effort enumeration of whole-disk block devices the current user
// can at least open for reading (Linux: /dev/sdX, /dev/nvmeXnY, etc. from
// /proc/partitions; Windows: \\.\PhysicalDriveN). Devices that fail to
// open (no permission, doesn't exist) are silently skipped - this is a
// convenience picker, not an inventory tool, and the Disk tab's path
// field is always free-text as a fallback.
QList<DiskDeviceEntry> enumerateDiskDevices();

} // namespace gui
