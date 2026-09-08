/*
  main.cpp - GUI entry point.
  C++/Qt port of insta360linkgui.lpr.
*/
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("insta360linkgui"));
    app.setApplicationDisplayName(QStringLiteral("Insta360 Link Controller"));

    MainWindow w;
    w.show();
    return app.exec();
}
