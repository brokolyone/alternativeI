#include "AffinityDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <thread>

#include "i18n.h"

namespace gui {

namespace {
// Matches IProcessProvider::affinityMask()'s "first 64 cores only" limit.
constexpr int kMaxCores = 64;
} // namespace

AffinityDialog::AffinityDialog(uint64_t currentMask, QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("CPU Affinity", "Привязка к процессорам"));

    unsigned int coreCount = std::thread::hardware_concurrency();
    if (coreCount == 0) {
        coreCount = 1; // hardware_concurrency() may return 0 if undetectable
    }
    coreCount = std::min<unsigned int>(coreCount, kMaxCores);

    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout();
    layout->addLayout(grid);

    constexpr int kColumns = 8;
    for (unsigned int core = 0; core < coreCount; ++core) {
        auto *box = new QCheckBox(QStringLiteral("CPU %1").arg(core), this);
        box->setChecked((currentMask & (uint64_t{1} << core)) != 0);
        grid->addWidget(box, static_cast<int>(core) / kColumns, static_cast<int>(core) % kColumns);
        checkboxes_.push_back(box);
    }

    auto *quickRow = new QVBoxLayout();
    auto *selectAll = new QPushButton(i18n::t("Select All", "Выбрать все"), this);
    auto *selectNone = new QPushButton(i18n::t("Select None", "Снять всё"), this);
    connect(selectAll, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : checkboxes_) box->setChecked(true);
    });
    connect(selectNone, &QPushButton::clicked, this, [this] {
        for (QCheckBox *box : checkboxes_) box->setChecked(false);
    });
    quickRow->addWidget(selectAll);
    quickRow->addWidget(selectNone);
    layout->addLayout(quickRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

uint64_t AffinityDialog::selectedMask() const {
    uint64_t mask = 0;
    for (size_t core = 0; core < checkboxes_.size(); ++core) {
        if (checkboxes_[core]->isChecked()) {
            mask |= (uint64_t{1} << core);
        }
    }
    return mask;
}

} // namespace gui
