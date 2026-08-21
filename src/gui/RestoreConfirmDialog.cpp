#include "RestoreConfirmDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "i18n.h"

namespace gui {

RestoreConfirmDialog::RestoreConfirmDialog(const QString &sourcePath, const QString &targetPath,
                                            quint64 offset, quint64 length, const QString &inputSha256,
                                            bool targetIsSpecialDevice, QWidget *parent)
    : QDialog(parent), targetPath_(targetPath) {
    setWindowTitle(i18n::t("Confirm restore", "Подтверждение восстановления"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);

    QString planText =
        i18n::t("<b>Source:</b> %1<br>"
                "<b>Source SHA-256:</b> %2<br>"
                "<b>Target:</b> %3%4<br>"
                "<b>Byte range:</b> [%5, %6)<br><br>",
                "<b>Источник:</b> %1<br>"
                "<b>SHA-256 источника:</b> %2<br>"
                "<b>Цель:</b> %3%4<br>"
                "<b>Диапазон байт:</b> [%5, %6)<br><br>")
            .arg(sourcePath.toHtmlEscaped(), inputSha256, targetPath.toHtmlEscaped(),
                 targetIsSpecialDevice ? i18n::t(" <b>(block device)</b>", " <b>(блочное устройство)</b>")
                                       : QString(),
                 QLocale().toString(static_cast<qlonglong>(offset)),
                 QLocale().toString(static_cast<qlonglong>(offset + length)));

    if (targetIsSpecialDevice) {
        planText += i18n::t(
            "<span style=\"color:#c0392b;\"><b>This writes directly to a block device.</b> "
            "Existing data in the byte range above will be permanently overwritten.</span><br><br>",
            "<span style=\"color:#c0392b;\"><b>Это прямая запись на блочное устройство.</b> "
            "Существующие данные в указанном диапазоне байт будут безвозвратно перезаписаны."
            "</span><br><br>");
    }

    planText += i18n::t("Type the target path below exactly to enable the Restore button:",
                         "Введите ниже точный путь цели, чтобы разблокировать кнопку «Восстановить»:");

    auto *planLabel = new QLabel(planText, this);
    planLabel->setWordWrap(true);
    planLabel->setTextFormat(Qt::RichText);
    layout->addWidget(planLabel);

    confirmEdit_ = new QLineEdit(this);
    confirmEdit_->setPlaceholderText(targetPath);
    layout->addWidget(confirmEdit_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    restoreButton_ = buttons->addButton(i18n::t("Restore", "Восстановить"), QDialogButtonBox::AcceptRole);
    restoreButton_->setEnabled(false);
    restoreButton_->setStyleSheet(QStringLiteral("QPushButton { font-weight: bold; }"));
    layout->addWidget(buttons);

    connect(confirmEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        restoreButton_->setEnabled(text == targetPath_);
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace gui
