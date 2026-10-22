#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ISACPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("ISACPlayer"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/isacplayer.png")));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
