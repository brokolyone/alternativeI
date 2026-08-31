#include <QApplication>

#include "gui/MainWindow.h"
#include "gui/Theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AltTools"));
    QApplication::setOrganizationName(QStringLiteral("alternativeI"));

    gui::theme::apply();

    gui::MainWindow window;
    window.show();

    return QApplication::exec();
}
