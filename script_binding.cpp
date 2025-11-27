#include "scriptapi.h"
#include "mainwindow.h"   // 必须包含以访问 MainWindow 的具体实现
#include "scriptwindow.h" // 包含以访问 ScriptWindow::appendLog
#include <pybind11/embed.h>
#include <pybind11/stl.h> // 支持 std::vector 到 python list 的自动转换
#include <QEventLoop>     // 用于同步等待
#include <QTimer>

namespace py = pybind11;

// --- ScriptAPI 实现 ---

ScriptAPI::ScriptAPI(MainWindow *mainWin) : m_mainWin(mainWin), m_scriptWin(nullptr) {}

void ScriptAPI::setScriptWindow(ScriptWindow *win) { m_scriptWin = win; }

void ScriptAPI::log(std::string msg)
{
    QString qmsg = QString::fromStdString(msg);
    if (m_scriptWin)
    {
        m_scriptWin->appendLog(qmsg);
    }
}

bool ScriptAPI::load_file(std::string path)
{
    if (!m_mainWin)
        return false;

    QString qPath = QString::fromStdString(path);
    // 统一路径分隔符，防止因斜杠方向不同导致字符串匹配失败
    QString cleanPath = QFileInfo(qPath).absoluteFilePath();

    log("Loading file: " + path + " ...");

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

    if (success)
    {
        // log("File loaded and UI updated.");
    }
    else
    {
        log("File load failed: " + errorMessage.toStdString());
    }

    return success;
}
void ScriptAPI::import_view(std::string path)
{
    if (m_mainWin)
    {
        QString qPath = QString::fromStdString(path);
        m_mainWin->importView(qPath);
        log("View imported from: " + path);
    }
}

void ScriptAPI::set_x_range(double min, double max)
{
    if (m_mainWin)
    {
        QCustomPlot *plot = m_mainWin->getActivePlot();
        if (plot)
        {
            plot->xAxis->setRange(min, max);
            plot->replot();
        }
        else
        {
            log("Warning: No active plot selected.");
        }
    }
}

void ScriptAPI::autoscale()
{
    if (m_mainWin)
    {
        m_mainWin->performFitView(true, true, MainWindow::FitActivePlot);
        // log("Autoscaled active plot.");
    }
}

void ScriptAPI::fit_view_y_all()
{
    if (m_mainWin)
    {
        m_mainWin->performFitView(false, true, MainWindow::FitAllPlots);
        // log("Applied Global Y-Axis Autoscale.");
    }
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

// --- Python 绑定定义 ---

PYBIND11_EMBEDDED_MODULE(inspector, m)
{
    py::class_<ScriptAPI>(m, "API")
        .def(py::init<MainWindow *>())
        .def("log", &ScriptAPI::log, "打印日志到控制台")
        .def("load_file", &ScriptAPI::load_file, "加载文件 (同步阻塞)，返回 True/False")
        .def("import_view", &ScriptAPI::import_view, "导入 .mldatx 视图布局")
        .def("set_x_range", &ScriptAPI::set_x_range, "设置当前子图 X 轴范围 (min, max)")
        .def("autoscale", &ScriptAPI::autoscale, "自适应当前子图")
        .def("fit_view_y_all", &ScriptAPI::fit_view_y_all, "所有子图 Y 轴自适应")
        .def("get_data", &ScriptAPI::get_data, "获取信号数据数组，参数为信号ID");
}