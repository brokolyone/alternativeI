#pragma once

#include <QDialog>

namespace gui {

// "About" dialog: what the program is, its version, license status, and
// how to reach the developer. Text is picked at construction time from the
// current i18n::currentLanguage() - see MainWindow's Settings/About wiring.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

} // namespace gui
