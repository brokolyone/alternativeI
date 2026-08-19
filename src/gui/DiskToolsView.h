#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QThread;

namespace gui {

class DiskWorker;

// "Disk" tab: a GUI front end for exactly the same diskutil_core logic
// the diskutil CLI uses (see DiskWorker/DiskOperations). Every write path
// goes through RestoreConfirmDialog's type-to-confirm gate; there is no
// "just click yes" restore action, matching the CLI's --yes-by-hand
// friction on purpose.
class DiskToolsView : public QWidget {
    Q_OBJECT

public:
    explicit DiskToolsView(QWidget *parent = nullptr);
    ~DiskToolsView() override;

private slots:
    void refreshDeviceList();
    void browseForImage();
    void showInfo();
    void startBackup();
    void startRestore();

    void onInfoReady(bool ok, const QString &error, const QString &text);
    void onBackupProgress(quint64 done, quint64 total);
    void onBackupFinished(bool ok, const QString &error, quint64 bytesCopied, const QString &sha256);
    void onRestorePlanReady(bool ok, const QString &error, quint64 offset, quint64 length,
                             quint64 inputSizeBytes, const QString &inputSha256,
                             bool targetIsSpecialDevice);
    void onRestoreProgress(quint64 done, quint64 total);
    void onRestoreFinished(bool ok, const QString &error, bool verified, const QString &expectedSha256,
                            const QString &actualSha256);

private:
    void buildUi();
    void setBusy(bool busy);
    void appendLog(const QString &line);
    int currentRegionKind() const;
    bool customRegionFields(quint64 *startLba, quint64 *sectors) const;

    QComboBox *pathCombo_;
    QComboBox *regionCombo_;
    QWidget *customRegionRow_;
    QLineEdit *startLbaEdit_;
    QLineEdit *sectorsEdit_;
    QPushButton *infoButton_;
    QPushButton *backupButton_;
    QPushButton *restoreButton_;
    QPushButton *refreshButton_;
    QProgressBar *progressBar_;
    QPlainTextEdit *log_;

    QThread *thread_;
    DiskWorker *worker_;

    // Set while a restore plan is pending confirmation, so the finished
    // plan can be matched back up with the input/target paths the user
    // picked (the worker signal only carries the plan itself).
    QString pendingRestoreInputPath_;
    QString pendingRestoreTargetPath_;
    int pendingRestoreRegionKind_ = 0;
    quint64 pendingRestoreStartLba_ = 0;
};

} // namespace gui
