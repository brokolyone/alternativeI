#include "SettingsDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QSpinBox>
#include <QVBoxLayout>

#include "Theme.h"
#include "i18n.h"

namespace gui {

SettingsDialog::SettingsDialog(int currentRefreshIntervalMs, QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("Settings", "Настройки"));
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    layout->addLayout(form);

    languageCombo_ = new QComboBox(this);
    // Index matches i18n::Language's underlying values (English=0, Russian=1).
    languageCombo_->addItem(QStringLiteral("English"));
    languageCombo_->addItem(QStringLiteral("Русский"));
    languageCombo_->setCurrentIndex(static_cast<int>(i18n::currentLanguage()));
    form->addRow(i18n::t("Language:", "Язык:"), languageCombo_);

    refreshIntervalSpin_ = new QSpinBox(this);
    refreshIntervalSpin_->setRange(250, 60000);
    refreshIntervalSpin_->setSingleStep(250);
    refreshIntervalSpin_->setSuffix(i18n::t(" ms", " мс"));
    refreshIntervalSpin_->setValue(currentRefreshIntervalMs);
    form->addRow(i18n::t("Process list refresh interval:", "Интервал обновления списка процессов:"),
                 refreshIntervalSpin_);

    themeCombo_ = new QComboBox(this);
    // Index matches gui::theme::Mode's underlying values (System=0, Light=1, Dark=2).
    themeCombo_->addItem(i18n::t("System", "Системная"));
    themeCombo_->addItem(i18n::t("Light", "Светлая"));
    themeCombo_->addItem(i18n::t("Dark", "Тёмная"));
    themeCombo_->setCurrentIndex(static_cast<int>(theme::currentMode()));
    form->addRow(i18n::t("Theme:", "Тема:"), themeCombo_);

    auto *hint = new QLabel(
        i18n::t("Changing the language takes effect after restarting AltTools.",
                "Смена языка вступит в силу после перезапуска AltTools."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int SettingsDialog::refreshIntervalMs() const {
    return refreshIntervalSpin_->value();
}

void SettingsDialog::accept() {
    const auto selectedTheme = static_cast<theme::Mode>(themeCombo_->currentIndex());
    if (selectedTheme != theme::currentMode()) {
        theme::setMode(selectedTheme);
        theme::apply();
    }

    const i18n::Language previousLanguage = i18n::currentLanguage();
    const auto selectedLanguage = static_cast<i18n::Language>(languageCombo_->currentIndex());
    i18n::setLanguage(selectedLanguage);

    if (selectedLanguage != previousLanguage) {
        const auto choice = QMessageBox::question(
            this, i18n::t("Restart required", "Требуется перезапуск"),
            i18n::t("The language change will take effect after AltTools restarts. Restart now?",
                    "Смена языка вступит в силу после перезапуска AltTools. Перезапустить сейчас?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice == QMessageBox::Yes) {
            // arguments() includes argv[0] (the program path itself); drop it
            // since QProcess::startDetached takes the program separately.
            QProcess::startDetached(QApplication::applicationFilePath(), QApplication::arguments().mid(1));
            QDialog::accept();
            qApp->quit();
            return;
        }
    }

    QDialog::accept();
}

} // namespace gui
