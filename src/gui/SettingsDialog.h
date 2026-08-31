#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;

namespace gui {

// "Settings" dialog: UI language (English/Russian, persisted via i18n -
// takes effect after a restart, which this dialog offers to do right
// away) and the process list's refresh interval (applied immediately by
// the caller via refreshIntervalMs() after exec() == Accepted).
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(int currentRefreshIntervalMs, QWidget *parent = nullptr);

    int refreshIntervalMs() const;

public slots:
    void accept() override;

private:
    QComboBox *languageCombo_;
    QComboBox *themeCombo_;
    QSpinBox *refreshIntervalSpin_;
};

} // namespace gui
