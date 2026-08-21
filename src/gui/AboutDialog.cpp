#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

#include "AppVersion.h"
#include "i18n.h"

namespace gui {

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("About AltTools", "О программе AltTools"));
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);

    auto *titleLabel = new QLabel(
        QStringLiteral("<b>%1</b> — %2 %3")
            .arg(QString::fromUtf8(kAppName), i18n::t("version", "версия"), QString::fromUtf8(kAppVersion)),
        this);
    layout->addWidget(titleLabel);

    auto *descriptionLabel = new QLabel(
        i18n::t("A cross-platform system utility for Windows and Linux: a process manager with a "
                "detailed process tree (threads, modules, memory regions, handles, network "
                "connections), live CPU/memory/disk/network performance graphs, a services manager "
                "(Windows services / systemd), and a separate diskutil module for SHA-256-verified "
                "raw MBR/GPT/disk backup and restore.",
                "Кроссплатформенная системная утилита для Windows и Linux: менеджер процессов с "
                "подробным деревом процессов (потоки, модули, области памяти, хендлы, сетевые "
                "соединения), графики производительности CPU/памяти/диска/сети в реальном времени, "
                "менеджер служб (Windows-службы / systemd), а также отдельный модуль diskutil для "
                "посекторного backup/restore MBR/GPT/дисков с проверкой SHA-256."),
        this);
    descriptionLabel->setWordWrap(true);
    layout->addWidget(descriptionLabel);

    auto *licenseLabel = new QLabel(
        i18n::t("License: not yet chosen — all rights reserved by the project's contributors "
                "pending that decision.",
                "Лицензия: пока не выбрана — все права принадлежат авторам проекта до её "
                "определения."),
        this);
    licenseLabel->setWordWrap(true);
    layout->addWidget(licenseLabel);

    auto *contactLabel = new QLabel(
        QStringLiteral("<b>%1</b>").arg(i18n::t("Developer contact (Telegram) — @vvooices",
                                                  "Связь с разработчиком (Telegram) — @vvooices")),
        this);
    layout->addWidget(contactLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

} // namespace gui
