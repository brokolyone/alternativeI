#include <QApplication>

#include "gui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Alternative Hacker"));
    QApplication::setOrganizationName(QStringLiteral("alternativeI"));

    gui::MainWindow window;
    window.show();

    return QApplication::exec();
}
