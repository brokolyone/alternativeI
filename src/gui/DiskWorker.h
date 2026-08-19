#pragma once

#include <QObject>
#include <QString>

namespace gui {

// Runs on its own QThread (owned by DiskToolsView) so backup/restore of a
// multi-gigabyte disk never blocks the UI. Every slot here maps directly
// to a diskutil_core call - see DiskOperations.h for the actual copy/
// verify logic and its safety invariants; this class is just Qt plumbing
// (signals for progress/results) around it.
//
// Region kind ints match core::RegionKind's declaration order
// (Mbr=0, Gpt=1, Disk=2, Custom=3) so callers can pass a QComboBox index
// straight through without a translation table.
class DiskWorker : public QObject {
    Q_OBJECT

public:
    explicit DiskWorker(QObject *parent = nullptr);

public slots:
    void doInfo(const QString &path);
    void doBackup(const QString &sourcePath, int regionKind, quint64 startLba, quint64 sectors,
                  const QString &outputPath);
    void doPlanRestore(const QString &inputPath, const QString &targetPath, int regionKind,
                        quint64 startLba);
    void doPerformRestore(const QString &inputPath, const QString &targetPath, int regionKind,
                           quint64 startLba);

signals:
    void infoReady(bool ok, QString error, QString text);

    void backupProgress(quint64 done, quint64 total);
    void backupFinished(bool ok, QString error, quint64 bytesCopied, QString sha256);

    void restorePlanReady(bool ok, QString error, quint64 offset, quint64 length, quint64 inputSizeBytes,
                           QString inputSha256, bool targetIsSpecialDevice);
    void restoreProgress(quint64 done, quint64 total);
    void restoreFinished(bool ok, QString error, bool verified, QString expectedSha256,
                          QString actualSha256);
};

} // namespace gui
