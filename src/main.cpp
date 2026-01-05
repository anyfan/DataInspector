#include "mainwindow.h"
#include <QApplication>

#ifdef ENABLE_PYTHON
#include <QDir>
#include <pybind11/embed.h>

namespace py = pybind11;
#endif

int main(int argc, char *argv[])
{
    // 启用高DPI缩放
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication a(argc, argv);

#ifdef ENABLE_PYTHON
    // 设置 Python Home 目录
    QString pythonHomePath = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/python-3.14.1-embed-amd64");

    _putenv(("PYTHONHOME=" + pythonHomePath.toStdString()).c_str());

    py::scoped_interpreter guard{};

    py::gil_scoped_release release;
#endif

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
