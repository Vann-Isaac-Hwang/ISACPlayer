#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ISACPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("ISACPlayer"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ISACPlayer"));
#if defined(Q_OS_LINUX)
    QGuiApplication::setDesktopFileName(QStringLiteral("isacplayer"));
#endif
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/isacplayer.png")));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
