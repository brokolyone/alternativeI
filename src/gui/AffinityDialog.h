#pragma once

#include <QDialog>
#include <cstdint>
#include <vector>

class QCheckBox;

namespace gui {

// Per-process CPU affinity picker: one checkbox per core, pre-checked
// from the mask the caller reads via IProcessProvider::affinityMask().
class AffinityDialog : public QDialog {
    Q_OBJECT

public:
    AffinityDialog(uint64_t currentMask, QWidget *parent = nullptr);

    uint64_t selectedMask() const;

private:
    std::vector<QCheckBox *> checkboxes_;
};

} // namespace gui
