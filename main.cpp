#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ISACPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("ISACPlayer"));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
