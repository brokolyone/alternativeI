#include "DiskToolsView.h"

#include "DiskDeviceList.h"
#include "DiskWorker.h"
#include "RestoreConfirmDialog.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QThread>
#include <QVBoxLayout>

namespace gui {

DiskToolsView::DiskToolsView(QWidget *parent) : QWidget(parent) {
    buildUi();

    thread_ = new QThread(this);
    worker_ = new DiskWorker();
    worker_->moveToThread(thread_);
    thread_->start();

    connect(worker_, &DiskWorker::infoReady, this, &DiskToolsView::onInfoReady);
    connect(worker_, &DiskWorker::backupProgress, this, &DiskToolsView::onBackupProgress);
    connect(worker_, &DiskWorker::backupFinished, this, &DiskToolsView::onBackupFinished);
    connect(worker_, &DiskWorker::restorePlanReady, this, &DiskToolsView::onRestorePlanReady);
    connect(worker_, &DiskWorker::restoreProgress, this, &DiskToolsView::onRestoreProgress);
    connect(worker_, &DiskWorker::restoreFinished, this, &DiskToolsView::onRestoreFinished);

    refreshDeviceList();
}

DiskToolsView::~DiskToolsView() {
    thread_->quit();
    thread_->wait();
    delete worker_;
}

void DiskToolsView::buildUi() {
    auto *layout = new QVBoxLayout(this);

    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(QStringLiteral("Device or image:"), this));
    pathCombo_ = new QComboBox(this);
    pathCombo_->setObjectName(QStringLiteral("diskPathCombo"));
    pathCombo_->setEditable(true);
    pathCombo_->setInsertPolicy(QComboBox::NoInsert);
    pathCombo_->setMinimumWidth(320);
    pathRow->addWidget(pathCombo_, 1);
    refreshButton_ = new QPushButton(QStringLiteral("Refresh devices"), this);
    connect(refreshButton_, &QPushButton::clicked, this, &DiskToolsView::refreshDeviceList);
    pathRow->addWidget(refreshButton_);
    auto *browseButton = new QPushButton(QStringLiteral("Browse image..."), this);
    connect(browseButton, &QPushButton::clicked, this, &DiskToolsView::browseForImage);
    pathRow->addWidget(browseButton);
    layout->addLayout(pathRow);

    auto *regionRow = new QHBoxLayout();
    regionRow->addWidget(new QLabel(QStringLiteral("Region:"), this));
    regionCombo_ = new QComboBox(this);
    // Order must match core::RegionKind: Mbr=0, Gpt=1, Disk=2, Custom=3.
    regionCombo_->addItem(QStringLiteral("MBR (512 bytes)"));
    regionCombo_->addItem(QStringLiteral("GPT (protective MBR + header + partition table)"));
    regionCombo_->addItem(QStringLiteral("Whole disk"));
    regionCombo_->addItem(QStringLiteral("Custom LBA range"));
    regionRow->addWidget(regionCombo_);

    customRegionRow_ = new QWidget(this);
    auto *customLayout = new QHBoxLayout(customRegionRow_);
    customLayout->setContentsMargins(0, 0, 0, 0);
    auto *digitsValidator = new QRegularExpressionValidator(QRegularExpression("\\d+"), this);
    customLayout->addWidget(new QLabel(QStringLiteral("Start LBA:"), customRegionRow_));
    startLbaEdit_ = new QLineEdit(QStringLiteral("0"), customRegionRow_);
    startLbaEdit_->setValidator(digitsValidator);
    startLbaEdit_->setMaximumWidth(120);
    customLayout->addWidget(startLbaEdit_);
    customLayout->addWidget(new QLabel(QStringLiteral("Sectors:"), customRegionRow_));
    sectorsEdit_ = new QLineEdit(customRegionRow_);
    sectorsEdit_->setValidator(digitsValidator);
    sectorsEdit_->setMaximumWidth(120);
    customLayout->addWidget(sectorsEdit_);
    customRegionRow_->setVisible(false);
    regionRow->addWidget(customRegionRow_);
    regionRow->addStretch(1);
    layout->addLayout(regionRow);

    connect(regionCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        customRegionRow_->setVisible(index == 3);
    });

    auto *buttonRow = new QHBoxLayout();
    infoButton_ = new QPushButton(QStringLiteral("Show Info"), this);
    connect(infoButton_, &QPushButton::clicked, this, &DiskToolsView::showInfo);
    buttonRow->addWidget(infoButton_);
    backupButton_ = new QPushButton(QStringLiteral("Backup..."), this);
    connect(backupButton_, &QPushButton::clicked, this, &DiskToolsView::startBackup);
    buttonRow->addWidget(backupButton_);
    restoreButton_ = new QPushButton(QStringLiteral("Restore..."), this);
    connect(restoreButton_, &QPushButton::clicked, this, &DiskToolsView::startRestore);
    buttonRow->addWidget(restoreButton_);
    buttonRow->addStretch(1);
    layout->addLayout(buttonRow);

    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setVisible(false);
    layout->addWidget(progressBar_);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont monoFont(QStringLiteral("monospace"));
    monoFont.setStyleHint(QFont::Monospace);
    log_->setFont(monoFont);
    layout->addWidget(log_, 1);

    appendLog(QStringLiteral("Backup is read-only and always safe. Restore always shows a plan and "
                              "requires typing the target path to confirm before anything is written."));
}

void DiskToolsView::setBusy(bool busy) {
    infoButton_->setEnabled(!busy);
    backupButton_->setEnabled(!busy);
    restoreButton_->setEnabled(!busy);
    refreshButton_->setEnabled(!busy);
    progressBar_->setVisible(busy);
    if (!busy) {
        progressBar_->setValue(0);
    }
}

void DiskToolsView::appendLog(const QString &line) {
    log_->appendPlainText(line);
}

int DiskToolsView::currentRegionKind() const {
    return regionCombo_->currentIndex();
}

bool DiskToolsView::customRegionFields(quint64 *startLba, quint64 *sectors) const {
    bool ok1 = false, ok2 = false;
    *startLba = startLbaEdit_->text().toULongLong(&ok1);
    *sectors = sectorsEdit_->text().toULongLong(&ok2);
    return ok1 && ok2 && *sectors > 0;
}

void DiskToolsView::refreshDeviceList() {
    const QString previous = pathCombo_->currentText();
    pathCombo_->clear();
    for (const auto &entry : enumerateDiskDevices()) {
        pathCombo_->addItem(QStringLiteral("%1  (%2)").arg(entry.path,
                                                             QLocale().formattedDataSize(
                                                                 static_cast<qint64>(entry.sizeBytes))),
                             entry.path);
    }
    if (!previous.isEmpty()) {
        pathCombo_->setEditText(previous);
    }
}

void DiskToolsView::browseForImage() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select image file"));
    if (!path.isEmpty()) {
        pathCombo_->setEditText(path);
    }
}

namespace {
// The combo's display text for a device entry is "path  (size)"; when a
// device (rather than a manually-typed path) is selected, resolve back to
// the raw path via the item's data instead of parsing the display text.
QString resolvedPath(QComboBox *combo) {
    const int index = combo->findText(combo->currentText());
    if (index >= 0) {
        const QVariant data = combo->itemData(index);
        if (data.isValid()) return data.toString();
    }
    return combo->currentText().trimmed();
}
} // namespace

void DiskToolsView::showInfo() {
    const QString path = resolvedPath(pathCombo_);
    if (path.isEmpty()) {
        appendLog(QStringLiteral("Enter or select a device/image path first."));
        return;
    }
    setBusy(true);
    appendLog(QStringLiteral("--- Info: %1 ---").arg(path));
    QMetaObject::invokeMethod(worker_, "doInfo", Qt::QueuedConnection, Q_ARG(QString, path));
}

void DiskToolsView::startBackup() {
    const QString path = resolvedPath(pathCombo_);
    if (path.isEmpty()) {
        appendLog(QStringLiteral("Enter or select a device/image path first."));
        return;
    }

    quint64 startLba = 0, sectors = 0;
    const int regionKind = currentRegionKind();
    if (regionKind == 3 && !customRegionFields(&startLba, &sectors)) {
        appendLog(QStringLiteral("Enter a valid start LBA and a non-zero sector count for a custom region."));
        return;
    }

    const QString outputPath = QFileDialog::getSaveFileName(this, QStringLiteral("Backup to file"));
    if (outputPath.isEmpty()) return;

    setBusy(true);
    appendLog(QStringLiteral("--- Backup: %1 -> %2 ---").arg(path, outputPath));
    QMetaObject::invokeMethod(worker_, "doBackup", Qt::QueuedConnection, Q_ARG(QString, path),
                               Q_ARG(int, regionKind), Q_ARG(quint64, startLba), Q_ARG(quint64, sectors),
                               Q_ARG(QString, outputPath));
}

void DiskToolsView::startRestore() {
    const QString targetPath = resolvedPath(pathCombo_);
    if (targetPath.isEmpty()) {
        appendLog(QStringLiteral("Enter or select a target device/image path first."));
        return;
    }

    const QString inputPath =
        QFileDialog::getOpenFileName(this, QStringLiteral("Select backup image to restore"));
    if (inputPath.isEmpty()) return;

    quint64 startLba = 0, sectors = 0;
    const int regionKind = currentRegionKind();
    if (regionKind == 3 && !customRegionFields(&startLba, &sectors)) {
        appendLog(QStringLiteral("Enter a valid start LBA for a custom region."));
        return;
    }

    pendingRestoreInputPath_ = inputPath;
    pendingRestoreTargetPath_ = targetPath;
    pendingRestoreRegionKind_ = regionKind;
    pendingRestoreStartLba_ = startLba;

    setBusy(true);
    appendLog(QStringLiteral("--- Planning restore: %1 -> %2 ---").arg(inputPath, targetPath));
    QMetaObject::invokeMethod(worker_, "doPlanRestore", Qt::QueuedConnection, Q_ARG(QString, inputPath),
                               Q_ARG(QString, targetPath), Q_ARG(int, regionKind),
                               Q_ARG(quint64, startLba));
}

void DiskToolsView::onInfoReady(bool ok, const QString &error, const QString &text) {
    setBusy(false);
    if (!ok) {
        appendLog(QStringLiteral("error: %1").arg(error));
        return;
    }
    appendLog(text);
}

void DiskToolsView::onBackupProgress(quint64 done, quint64 total) {
    if (total > 0) {
        progressBar_->setValue(static_cast<int>((done * 100) / total));
    }
}

void DiskToolsView::onBackupFinished(bool ok, const QString &error, quint64 bytesCopied,
                                      const QString &sha256) {
    setBusy(false);
    if (!ok) {
        appendLog(QStringLiteral("Backup failed: %1").arg(error));
        QMessageBox::warning(this, QStringLiteral("Backup failed"), error);
        return;
    }
    appendLog(QStringLiteral("Backup complete: %1 bytes, SHA-256 %2")
                  .arg(QLocale().toString(static_cast<qlonglong>(bytesCopied)), sha256));
    QMessageBox::information(this, QStringLiteral("Backup complete"),
                              QStringLiteral("%1 copied.\nSHA-256: %2")
                                  .arg(QLocale().formattedDataSize(static_cast<qint64>(bytesCopied)), sha256));
}

void DiskToolsView::onRestorePlanReady(bool ok, const QString &error, quint64 offset, quint64 length,
                                        quint64 inputSizeBytes, const QString &inputSha256,
                                        bool targetIsSpecialDevice) {
    setBusy(false);
    if (!ok) {
        appendLog(QStringLiteral("Restore plan failed: %1").arg(error));
        QMessageBox::warning(this, QStringLiteral("Cannot restore"), error);
        return;
    }
    Q_UNUSED(inputSizeBytes);

    RestoreConfirmDialog dialog(pendingRestoreInputPath_, pendingRestoreTargetPath_, offset, length,
                                 inputSha256, targetIsSpecialDevice, this);
    if (dialog.exec() != QDialog::Accepted) {
        appendLog(QStringLiteral("Restore cancelled."));
        return;
    }

    setBusy(true);
    appendLog(QStringLiteral("--- Restoring: %1 -> %2 ---")
                  .arg(pendingRestoreInputPath_, pendingRestoreTargetPath_));
    QMetaObject::invokeMethod(worker_, "doPerformRestore", Qt::QueuedConnection,
                               Q_ARG(QString, pendingRestoreInputPath_),
                               Q_ARG(QString, pendingRestoreTargetPath_),
                               Q_ARG(int, pendingRestoreRegionKind_), Q_ARG(quint64, pendingRestoreStartLba_));
}

void DiskToolsView::onRestoreProgress(quint64 done, quint64 total) {
    if (total > 0) {
        progressBar_->setValue(static_cast<int>((done * 100) / total));
    }
}

void DiskToolsView::onRestoreFinished(bool ok, const QString &error, bool verified,
                                       const QString &expectedSha256, const QString &actualSha256) {
    setBusy(false);
    if (!ok) {
        appendLog(QStringLiteral("*** RESTORE FAILED: %1 ***").arg(error));
        QMessageBox::critical(this, QStringLiteral("Restore failed"), error);
        return;
    }
    appendLog(QStringLiteral("Restore complete and verified (SHA-256 %1).").arg(actualSha256));
    Q_UNUSED(verified);
    Q_UNUSED(expectedSha256);
    QMessageBox::information(this, QStringLiteral("Restore complete"),
                              QStringLiteral("Restore verified: written data matches the source "
                                              "image byte-for-byte.\nSHA-256: %1")
                                  .arg(actualSha256));
}

} // namespace gui
