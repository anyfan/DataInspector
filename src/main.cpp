#include "mainwindow.h"
#include <QApplication>
#include <pybind11/embed.h>

namespace py = pybind11;

int main(int argc, char *argv[])
{
    // 启用高DPI缩放
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    // 初始化 Python 解释器
    py::scoped_interpreter guard{};

    QApplication a(argc, argv);

    QIcon appIcon(":/icon/plotjuggler.svg");
    if (!appIcon.isNull())
    {
        a.setWindowIcon(appIcon);
    }

    MainWindow w;
    w.resize(1024, 768);
    w.show();

    return a.exec();
}