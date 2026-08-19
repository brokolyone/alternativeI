#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;

namespace gui {

// Restore's last line of defense before anything gets written: shows the
// exact plan (source, target, byte range, source SHA-256) and keeps the
// "Restore" button disabled until the user retypes the target path
// verbatim - the same "type the resource name to confirm" pattern used
// for other hard-to-undo actions, chosen over a plain Yes/No box because
// a reflexive click is much easier than a reflexive retype.
class RestoreConfirmDialog : public QDialog {
    Q_OBJECT

public:
    RestoreConfirmDialog(const QString &sourcePath, const QString &targetPath, quint64 offset,
                          quint64 length, const QString &inputSha256, bool targetIsSpecialDevice,
                          QWidget *parent = nullptr);

private:
    QString targetPath_;
    QLineEdit *confirmEdit_;
    QPushButton *restoreButton_;
};

} // namespace gui
