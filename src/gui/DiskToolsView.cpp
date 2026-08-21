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

#include "i18n.h"

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
    pathRow->addWidget(new QLabel(i18n::t("Device or image:", "Устройство или образ:"), this));
    pathCombo_ = new QComboBox(this);
    pathCombo_->setObjectName(QStringLiteral("diskPathCombo"));
    pathCombo_->setEditable(true);
    pathCombo_->setInsertPolicy(QComboBox::NoInsert);
    pathCombo_->setMinimumWidth(320);
    pathRow->addWidget(pathCombo_, 1);
    refreshButton_ = new QPushButton(i18n::t("Refresh devices", "Обновить устройства"), this);
    connect(refreshButton_, &QPushButton::clicked, this, &DiskToolsView::refreshDeviceList);
    pathRow->addWidget(refreshButton_);
    auto *browseButton = new QPushButton(i18n::t("Browse image...", "Выбрать образ..."), this);
    connect(browseButton, &QPushButton::clicked, this, &DiskToolsView::browseForImage);
    pathRow->addWidget(browseButton);
    layout->addLayout(pathRow);

    auto *regionRow = new QHBoxLayout();
    regionRow->addWidget(new QLabel(i18n::t("Region:", "Область:"), this));
    regionCombo_ = new QComboBox(this);
    // Order must match core::RegionKind: Mbr=0, Gpt=1, Disk=2, Custom=3.
    regionCombo_->addItem(i18n::t("MBR (512 bytes)", "MBR (512 байт)"));
    regionCombo_->addItem(i18n::t("GPT (protective MBR + header + partition table)",
                                   "GPT (защитный MBR + заголовок + таблица разделов)"));
    regionCombo_->addItem(i18n::t("Whole disk", "Весь диск"));
    regionCombo_->addItem(i18n::t("Custom LBA range", "Произвольный диапазон LBA"));
    regionRow->addWidget(regionCombo_);

    customRegionRow_ = new QWidget(this);
    auto *customLayout = new QHBoxLayout(customRegionRow_);
    customLayout->setContentsMargins(0, 0, 0, 0);
    auto *digitsValidator = new QRegularExpressionValidator(QRegularExpression("\\d+"), this);
    customLayout->addWidget(new QLabel(i18n::t("Start LBA:", "Начальный LBA:"), customRegionRow_));
    startLbaEdit_ = new QLineEdit(QStringLiteral("0"), customRegionRow_);
    startLbaEdit_->setValidator(digitsValidator);
    startLbaEdit_->setMaximumWidth(120);
    customLayout->addWidget(startLbaEdit_);
    customLayout->addWidget(new QLabel(i18n::t("Sectors:", "Секторов:"), customRegionRow_));
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
    infoButton_ = new QPushButton(i18n::t("Show Info", "Показать информацию"), this);
    connect(infoButton_, &QPushButton::clicked, this, &DiskToolsView::showInfo);
    buttonRow->addWidget(infoButton_);
    backupButton_ = new QPushButton(i18n::t("Backup...", "Резервная копия..."), this);
    connect(backupButton_, &QPushButton::clicked, this, &DiskToolsView::startBackup);
    buttonRow->addWidget(backupButton_);
    restoreButton_ = new QPushButton(i18n::t("Restore...", "Восстановить..."), this);
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

    appendLog(i18n::t("Backup is read-only and always safe. Restore always shows a plan and "
                       "requires typing the target path to confirm before anything is written.",
                       "Резервное копирование доступно только для чтения и всегда безопасно. "
                       "Восстановление всегда показывает план и требует ввода пути цели для "
                       "подтверждения перед записью."));
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
    const QString path =
        QFileDialog::getOpenFileName(this, i18n::t("Select image file", "Выберите файл образа"));
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
        appendLog(i18n::t("Enter or select a device/image path first.",
                           "Сначала введите или выберите путь к устройству/образу."));
        return;
    }
    setBusy(true);
    appendLog(i18n::t("--- Info: %1 ---", "--- Информация: %1 ---").arg(path));
    QMetaObject::invokeMethod(worker_, "doInfo", Qt::QueuedConnection, Q_ARG(QString, path));
}

void DiskToolsView::startBackup() {
    const QString path = resolvedPath(pathCombo_);
    if (path.isEmpty()) {
        appendLog(i18n::t("Enter or select a device/image path first.",
                           "Сначала введите или выберите путь к устройству/образу."));
        return;
    }

    quint64 startLba = 0, sectors = 0;
    const int regionKind = currentRegionKind();
    if (regionKind == 3 && !customRegionFields(&startLba, &sectors)) {
        appendLog(i18n::t("Enter a valid start LBA and a non-zero sector count for a custom region.",
                           "Введите корректный начальный LBA и ненулевое число секторов для "
                           "произвольной области."));
        return;
    }

    const QString outputPath =
        QFileDialog::getSaveFileName(this, i18n::t("Backup to file", "Сохранить резервную копию в файл"));
    if (outputPath.isEmpty()) return;

    setBusy(true);
    appendLog(i18n::t("--- Backup: %1 -> %2 ---", "--- Резервная копия: %1 -> %2 ---").arg(path, outputPath));
    QMetaObject::invokeMethod(worker_, "doBackup", Qt::QueuedConnection, Q_ARG(QString, path),
                               Q_ARG(int, regionKind), Q_ARG(quint64, startLba), Q_ARG(quint64, sectors),
                               Q_ARG(QString, outputPath));
}

void DiskToolsView::startRestore() {
    const QString targetPath = resolvedPath(pathCombo_);
    if (targetPath.isEmpty()) {
        appendLog(i18n::t("Enter or select a target device/image path first.",
                           "Сначала введите или выберите путь к целевому устройству/образу."));
        return;
    }

    const QString inputPath = QFileDialog::getOpenFileName(
        this, i18n::t("Select backup image to restore", "Выберите файл резервной копии для восстановления"));
    if (inputPath.isEmpty()) return;

    quint64 startLba = 0, sectors = 0;
    const int regionKind = currentRegionKind();
    if (regionKind == 3 && !customRegionFields(&startLba, &sectors)) {
        appendLog(i18n::t("Enter a valid start LBA for a custom region.",
                           "Введите корректный начальный LBA для произвольной области."));
        return;
    }

    pendingRestoreInputPath_ = inputPath;
    pendingRestoreTargetPath_ = targetPath;
    pendingRestoreRegionKind_ = regionKind;
    pendingRestoreStartLba_ = startLba;

    setBusy(true);
    appendLog(i18n::t("--- Planning restore: %1 -> %2 ---", "--- Планирование восстановления: %1 -> %2 ---")
                  .arg(inputPath, targetPath));
    QMetaObject::invokeMethod(worker_, "doPlanRestore", Qt::QueuedConnection, Q_ARG(QString, inputPath),
                               Q_ARG(QString, targetPath), Q_ARG(int, regionKind),
                               Q_ARG(quint64, startLba));
}

void DiskToolsView::onInfoReady(bool ok, const QString &error, const QString &text) {
    setBusy(false);
    if (!ok) {
        appendLog(i18n::t("error: %1", "ошибка: %1").arg(error));
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
        appendLog(i18n::t("Backup failed: %1", "Резервное копирование не удалось: %1").arg(error));
        QMessageBox::warning(this, i18n::t("Backup failed", "Резервное копирование не удалось"), error);
        return;
    }
    appendLog(i18n::t("Backup complete: %1 bytes, SHA-256 %2", "Резервная копия готова: %1 байт, SHA-256 %2")
                  .arg(QLocale().toString(static_cast<qlonglong>(bytesCopied)), sha256));
    QMessageBox::information(
        this, i18n::t("Backup complete", "Резервная копия готова"),
        i18n::t("%1 copied.\nSHA-256: %2", "Скопировано: %1.\nSHA-256: %2")
            .arg(QLocale().formattedDataSize(static_cast<qint64>(bytesCopied)), sha256));
}

void DiskToolsView::onRestorePlanReady(bool ok, const QString &error, quint64 offset, quint64 length,
                                        quint64 inputSizeBytes, const QString &inputSha256,
                                        bool targetIsSpecialDevice) {
    setBusy(false);
    if (!ok) {
        appendLog(i18n::t("Restore plan failed: %1", "Не удалось построить план восстановления: %1")
                      .arg(error));
        QMessageBox::warning(this, i18n::t("Cannot restore", "Восстановление невозможно"), error);
        return;
    }
    Q_UNUSED(inputSizeBytes);

    RestoreConfirmDialog dialog(pendingRestoreInputPath_, pendingRestoreTargetPath_, offset, length,
                                 inputSha256, targetIsSpecialDevice, this);
    if (dialog.exec() != QDialog::Accepted) {
        appendLog(i18n::t("Restore cancelled.", "Восстановление отменено."));
        return;
    }

    setBusy(true);
    appendLog(i18n::t("--- Restoring: %1 -> %2 ---", "--- Восстановление: %1 -> %2 ---")
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
        appendLog(i18n::t("*** RESTORE FAILED: %1 ***", "*** ВОССТАНОВЛЕНИЕ НЕ УДАЛОСЬ: %1 ***").arg(error));
        QMessageBox::critical(this, i18n::t("Restore failed", "Восстановление не удалось"), error);
        return;
    }
    appendLog(i18n::t("Restore complete and verified (SHA-256 %1).",
                       "Восстановление завершено и проверено (SHA-256 %1).")
                  .arg(actualSha256));
    Q_UNUSED(verified);
    Q_UNUSED(expectedSha256);
    QMessageBox::information(
        this, i18n::t("Restore complete", "Восстановление завершено"),
        i18n::t("Restore verified: written data matches the source image byte-for-byte.\nSHA-256: %1",
                "Проверено: записанные данные побайтово совпадают с исходным образом.\nSHA-256: %1")
            .arg(actualSha256));
}

} // namespace gui
