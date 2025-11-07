#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // 启用高DPI缩放
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);
    MainWindow w;
    w.resize(1024, 768); // 设置一个默认大小
    w.show();

    return a.exec();
}