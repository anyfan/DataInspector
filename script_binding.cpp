#include "scriptapi.h"
#include "mainwindow.h"
#include "scriptwindow.h"
#include "signalbrowser.h"
#include "qcustomplot.h"
#include "plotmanager.h"
#include "types.h"
#include <QStandardItem>
#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

ScriptAPI::ScriptAPI(MainWindow *mainWin) : m_mainWin(mainWin), m_scriptWin(nullptr) {}

void ScriptAPI::setScriptWindow(ScriptWindow *win) { m_scriptWin = win; }

void ScriptAPI::log(std::string msg)
{
    if (m_scriptWin)
        m_scriptWin->appendLog(QString::fromStdString(msg));
}

bool ScriptAPI::load_file(std::string path, bool overwrite)
{
    if (!m_mainWin)
        return false;

    QString qPath = QString::fromStdString(path);
    QString cleanPath = QFileInfo(qPath).absoluteFilePath();
    QString fileName = QFileInfo(cleanPath).fileName();

    if (overwrite && m_mainWin->m_fileDataMap.contains(fileName))
    {
        m_mainWin->removeFile(fileName);
        log("Overwriting file: " + fileName.toStdString());
    }

    QEventLoop loop;
    bool success = false;
    QString errorMessage;
    DataManager *dm = m_mainWin->m_dataManager;

    auto connSuccess = QObject::connect(m_mainWin, &MainWindow::dataProcessingFinished,
                                        [&](const QString &filePath)
                                        {
                                            // 校验路径是否匹配
                                            if (QFileInfo(filePath).absoluteFilePath() == cleanPath)
                                            {
                                                success = true;
                                                loop.quit();
                                            }
                                        });

    auto connFail = QObject::connect(dm, &DataManager::loadFailed,
                                     [&](const QString &filePath, const QString &err)
                                     {
                                         if (QFileInfo(filePath).absoluteFilePath() == cleanPath)
                                         {
                                             success = false;
                                             errorMessage = err;
                                             loop.quit();
                                         }
                                     });

    // 发起加载
    m_mainWin->loadFile(qPath);

    // 等待
    loop.exec();

    // 断开连接
    QObject::disconnect(connSuccess);
    QObject::disconnect(connFail);

    if (!success)
    {
        log("File load failed: " + errorMessage.toStdString());
    }

    return success;
}

bool ScriptAPI::remove_file(std::string filename)
{
    if (!m_mainWin)
        return false;
    QString qName = QString::fromStdString(filename);
    m_mainWin->removeFile(qName);
    return true;
}

void ScriptAPI::import_view(std::string path)
{
    if (!m_mainWin)
        return;
    QString qPath = QString::fromStdString(path);

    QEventLoop loop;
    // 连接信号
    QObject::connect(m_mainWin, &MainWindow::viewImportFinished, &loop, &QEventLoop::quit);

    // 使用 Timer 异步触发，确保 loop.exec() 在信号发射前启动
    QTimer::singleShot(0, m_mainWin, [this, qPath]()
                       { m_mainWin->importView(qPath); });

    loop.exec();
}

std::string ScriptAPI::find_id(std::string name)
{
    if (!m_mainWin || !m_mainWin->m_signalBrowser)
        return "";

    QString qName = QString::fromStdString(name);
    // 使用 SignalBrowser 的公共方法查找 Item
    QStandardItem *item = m_mainWin->m_signalBrowser->findItemByName(qName);

    if (item)
    {
        // 提取存储在 UserRole 中的 ID
        QString id = item->data(TreeItemRoles::UniqueIdRole).toString();
        // log("Found ID: " + id.toStdString());
        return id.toStdString();
    }

    log("Signal name not found: " + name);
    return "";
}

void ScriptAPI::set_x_range(double min, double max)
{
    if (!m_mainWin || !m_mainWin->getPlotManager()->getActivePlot())
        return;
    QCustomPlot *plot = m_mainWin->getPlotManager()->getActivePlot();
    plot->xAxis->setRange(min, max);
    plot->replot();
}

void ScriptAPI::set_y_range(double min, double max)
{
    if (!m_mainWin || !m_mainWin->getPlotManager()->getActivePlot())
        return;
    QCustomPlot *plot = m_mainWin->getPlotManager()->getActivePlot();
    plot->yAxis->setRange(min, max);
    plot->replot();
}

void ScriptAPI::autoscale()
{
    if (!m_mainWin)
        return;
    m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitActivePlot);
}

void ScriptAPI::fit_view_y_all()
{
    if (!m_mainWin)
        return;
    m_mainWin->getPlotManager()->performFitView(false, true, PlotManager::FitAllPlots);
}
std::vector<double> ScriptAPI::get_data(std::string id)
{
    if (!m_mainWin)
        return {};

    QString qid = QString::fromStdString(id);
    // 使用 MainWindow 的辅助函数查找数据位置
    SignalLocation loc = m_mainWin->getSignalDataFromID(qid);

    if (loc.table && loc.signalIndex >= 0 && loc.signalIndex < loc.table->valueData.size())
    {
        const QVector<double> &qvec = loc.table->valueData[loc.signalIndex];
        // 转换为 std::vector 供 Python 使用
        return std::vector<double>(qvec.begin(), qvec.end());
    }

    log("Error: Signal ID not found or invalid: " + id);
    return {};
}

// 获取时间数据实现
std::vector<double> ScriptAPI::get_time_data(std::string id)
{
    if (!m_mainWin)
        return {};

    QString qid = QString::fromStdString(id);
    // 使用 MainWindow 的辅助函数查找数据位置
    SignalLocation loc = m_mainWin->getSignalDataFromID(qid);

    // 只要找到了 Table，就可以获取 TimeData
    if (loc.table)
    {
        const QVector<double> &qvec = loc.table->timeData;
        return std::vector<double>(qvec.begin(), qvec.end());
    }

    log("Error: Signal ID not found or invalid: " + id);
    return {};
}

bool ScriptAPI::export_plot(std::string path)
{
    if (!m_mainWin)
        return false;
    m_mainWin->getPlotManager()->exportActivePlot(QString::fromStdString(path));
    return true;
}

bool ScriptAPI::export_view(std::string path)
{
    if (!m_mainWin)
        return false;
    m_mainWin->getPlotManager()->exportAllViews(QString::fromStdString(path));
    return true;
}

PYBIND11_EMBEDDED_MODULE(inspector, m)
{
    py::class_<ScriptAPI>(m, "API")
        .def(py::init<MainWindow *>())
        .def("log", &ScriptAPI::log, "打印日志到控制台")
        .def("load_file", &ScriptAPI::load_file, "加载文件 (同步阻塞)，可选参数 overwrite=True/False",
             py::arg("path"), py::arg("overwrite") = false)
        .def("remove_file", &ScriptAPI::remove_file, "移除已加载的文件 (参数为文件名，非路径)")
        .def("import_view", &ScriptAPI::import_view, "导入 .mldatx 视图布局")
        .def("find_id", &ScriptAPI::find_id, "根据信号名称查找其唯一 ID")
        .def("set_x_range", &ScriptAPI::set_x_range, "设置当前子图 X 轴范围 (min, max)")
        .def("set_y_range", &ScriptAPI::set_y_range, "设置当前子图 Y 轴范围 (min, max)")
        .def("autoscale", &ScriptAPI::autoscale, "自适应当前子图")
        .def("fit_view_y_all", &ScriptAPI::fit_view_y_all, "所有子图 Y 轴自适应")
        .def("get_data", &ScriptAPI::get_data, "获取信号数据数组，参数为信号ID")
        .def("get_time_data", &ScriptAPI::get_time_data, "获取信号时间数组，参数为信号ID")
        .def("export_plot", &ScriptAPI::export_plot, "导出当前激活子图为图片 (png, jpg, pdf)")
        .def("export_view", &ScriptAPI::export_view, "导出整个主界面视图布局为图片");
}