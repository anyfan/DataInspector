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
    // scoped_interpreter 会初始化 Python 并默认持有 GIL
    py::scoped_interpreter guard{};

    // 关键：释放 GIL，允许其他线程（我们的工作线程）获取它来运行 Python 代码
    // 主线程在需要调用 Python C API 时会自动重新获取（pybind11 处理），或者我们需要手动获取
    py::gil_scoped_release release;

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