#include "RestoreConfirmDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace gui {

RestoreConfirmDialog::RestoreConfirmDialog(const QString &sourcePath, const QString &targetPath,
                                            quint64 offset, quint64 length, const QString &inputSha256,
                                            bool targetIsSpecialDevice, QWidget *parent)
    : QDialog(parent), targetPath_(targetPath) {
    setWindowTitle(QStringLiteral("Confirm restore"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);

    QString planText = QStringLiteral("<b>Source:</b> %1<br>"
                                       "<b>Source SHA-256:</b> %2<br>"
                                       "<b>Target:</b> %3%4<br>"
                                       "<b>Byte range:</b> [%5, %6)<br><br>")
                            .arg(sourcePath.toHtmlEscaped(), inputSha256,
                                 targetPath.toHtmlEscaped(),
                                 targetIsSpecialDevice ? QStringLiteral(" <b>(block device)</b>") : QString(),
                                 QLocale().toString(static_cast<qlonglong>(offset)),
                                 QLocale().toString(static_cast<qlonglong>(offset + length)));

    if (targetIsSpecialDevice) {
        planText += QStringLiteral(
            "<span style=\"color:#c0392b;\"><b>This writes directly to a block device.</b> "
            "Existing data in the byte range above will be permanently overwritten.</span><br><br>");
    }

    planText += QStringLiteral("Type the target path below exactly to enable the Restore button:");

    auto *planLabel = new QLabel(planText, this);
    planLabel->setWordWrap(true);
    planLabel->setTextFormat(Qt::RichText);
    layout->addWidget(planLabel);

    confirmEdit_ = new QLineEdit(this);
    confirmEdit_->setPlaceholderText(targetPath);
    layout->addWidget(confirmEdit_);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    restoreButton_ = buttons->addButton(QStringLiteral("Restore"), QDialogButtonBox::AcceptRole);
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
