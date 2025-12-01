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
        return id.toStdString();
    }

    log("Signal name not found: " + name);
    return "";
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

QCustomPlot *ScriptAPI::getTargetPlot(int view_index)
{
    if (!m_mainWin || !m_mainWin->getPlotManager())
        return nullptr;

    // 如果 index 为 -1，使用当前激活的 Plot
    if (view_index < 0)
        return m_mainWin->getPlotManager()->getActivePlot();

    // 否则按索引获取
    auto &plots = m_mainWin->getPlotManager()->getPlots();
    if (view_index >= 0 && view_index < plots.size())
        return plots[view_index];

    return nullptr;
}

// 获取所有信号ID
std::vector<std::string> ScriptAPI::get_all_signal_ids()
{
    std::vector<std::string> result;
    if (m_mainWin && m_mainWin->m_signalBrowser)
    {
        QStringList ids = m_mainWin->m_signalBrowser->getAllSignalIDs();
        for (const QString &s : ids)
            result.push_back(s.toStdString());
    }
    return result;
}

// 设置布局
void ScriptAPI::set_layout(int rows, int cols)
{
    if (m_mainWin && m_mainWin->getPlotManager())
        m_mainWin->getPlotManager()->setupLayout(rows, cols);
}

// 获取视图数量
int ScriptAPI::get_view_count()
{
    if (m_mainWin && m_mainWin->getPlotManager())
        return m_mainWin->getPlotManager()->getPlotCount();
    return 0;
}

//  获取激活视图索引
int ScriptAPI::get_active_view_index()
{
    if (m_mainWin && m_mainWin->getPlotManager())
        return m_mainWin->getPlotManager()->getActivePlotIndex();
    return -1;
}

//  设置激活视图
void ScriptAPI::set_active_view(int index)
{
    if (m_mainWin && m_mainWin->getPlotManager())
        m_mainWin->getPlotManager()->setActivePlotIndex(index);
}

// 支持指定视图
void ScriptAPI::set_x_range(double min, double max, int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        plot->xAxis->setRange(min, max);
        plot->replot();
    }
}

// 支持指定视图
void ScriptAPI::set_y_range(double min, double max, int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        plot->yAxis->setRange(min, max);
        plot->replot();
    }
}

//  获取 X 轴范围
std::tuple<double, double> ScriptAPI::get_x_range(int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        return std::make_tuple(plot->xAxis->range().lower, plot->xAxis->range().upper);
    }
    return std::make_tuple(0.0, 1.0);
}

//  获取 Y 轴范围
std::tuple<double, double> ScriptAPI::get_y_range(int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        return std::make_tuple(plot->yAxis->range().lower, plot->yAxis->range().upper);
    }
    return std::make_tuple(0.0, 1.0);
}

// 支持指定视图
void ScriptAPI::autoscale(int view_index)
{
    if (!m_mainWin)
        return;

    if (view_index < 0)
    {
        // 自适应当前
        m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitActivePlot);
    }
    else
    {
        // 自适应指定
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot)
        {
            QList<QCustomPlot *> targets;
            targets << plot;
            m_mainWin->getPlotManager()->performFitView(targets, true, true);
        }
    }
}

//  获取视图中的信号列表
std::vector<std::string> ScriptAPI::get_view_signals(int view_index)
{
    std::vector<std::string> result;
    if (!m_mainWin || !m_mainWin->getPlotManager())
        return result;

    int idx = view_index;
    if (idx < 0)
        idx = get_active_view_index();

    QSet<QString> ids = m_mainWin->getPlotManager()->getPlotSignalIDs(idx);
    for (const QString &s : ids)
        result.push_back(s.toStdString());
    return result;
}

//  添加信号到视图
bool ScriptAPI::add_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;

    QCustomPlot *plot = getTargetPlot(view_index);
    if (!plot)
        return false;

    QString qid = QString::fromStdString(id);
    SignalLocation loc = m_mainWin->getSignalDataFromID(qid);

    if (loc.table)
    {
        // 调用 MainWindow 的逻辑或直接调用 PlotManager
        m_mainWin->getPlotManager()->addSignal(qid, loc, plot);
        // 如果是当前激活视图，还需要更新 SignalBrowser 的勾选状态
        if (plot == m_mainWin->getPlotManager()->getActivePlot())
        {
            m_mainWin->m_signalBrowser->setSignalChecked(qid, true, true);
        }
        return true;
    }
    return false;
}

//  从视图移除信号
bool ScriptAPI::remove_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;

    QCustomPlot *plot = getTargetPlot(view_index);
    if (!plot)
        return false;

    QString qid = QString::fromStdString(id);
    m_mainWin->getPlotManager()->removeSignal(qid, plot);

    // 如果是当前激活视图，更新 UI
    if (plot == m_mainWin->getPlotManager()->getActivePlot())
    {
        m_mainWin->m_signalBrowser->setSignalChecked(qid, false, true);
    }
    return true;
}

std::string ScriptAPI::get_signal_name(std::string id)
{
    if (!m_mainWin || !m_mainWin->m_signalBrowser)
        return "";

    // 调用 SignalBrowser 现有的 getSignalName 方法
    QString name = m_mainWin->m_signalBrowser->getSignalName(QString::fromStdString(id));
    return name.toStdString();
}

// --- Python 模块定义 ---
PYBIND11_EMBEDDED_MODULE(inspector, m)
{
    py::class_<ScriptAPI>(m, "API")
        .def(py::init<MainWindow *>())
        .def("log", &ScriptAPI::log)
        // 文件
        .def("load_file", &ScriptAPI::load_file, py::arg("path"), py::arg("overwrite") = false)
        .def("remove_file", &ScriptAPI::remove_file)
        .def("import_view", &ScriptAPI::import_view)
        .def("export_plot", &ScriptAPI::export_plot)
        .def("export_view", &ScriptAPI::export_view)

        // 数据
        .def("find_id", &ScriptAPI::find_id)
        .def("get_data", &ScriptAPI::get_data)
        .def("get_time_data", &ScriptAPI::get_time_data)
        .def("get_all_signal_ids", &ScriptAPI::get_all_signal_ids, "获取所有已加载信号的ID列表")
        .def("get_signal_name", &ScriptAPI::get_signal_name, "根据信号ID获取信号名称", py::arg("id"))

        // 布局与视图管理
        .def("set_layout", &ScriptAPI::set_layout, "设置布局 (rows, cols)", py::arg("rows"), py::arg("cols"))
        .def("get_view_count", &ScriptAPI::get_view_count, "获取子图总数")
        .def("get_active_view_index", &ScriptAPI::get_active_view_index, "获取当前激活子图的索引 (0-based)")
        .def("set_active_view", &ScriptAPI::set_active_view, "设置当前激活子图", py::arg("index"))

        // 视图信号操作
        .def("get_view_signals", &ScriptAPI::get_view_signals, "获取指定视图中的信号ID列表", py::arg("view_index") = -1)
        .def("add_signal", &ScriptAPI::add_signal, "添加信号到视图", py::arg("id"), py::arg("view_index") = -1)
        .def("remove_signal", &ScriptAPI::remove_signal, "从视图移除信号", py::arg("id"), py::arg("view_index") = -1)

        // 坐标轴操作
        .def("set_x_range", &ScriptAPI::set_x_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("set_y_range", &ScriptAPI::set_y_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("get_x_range", &ScriptAPI::get_x_range, "返回 (min, max)", py::arg("view_index") = -1)
        .def("get_y_range", &ScriptAPI::get_y_range, "返回 (min, max)", py::arg("view_index") = -1)
        .def("autoscale", &ScriptAPI::autoscale, py::arg("view_index") = -1)
        .def("fit_view_y_all", &ScriptAPI::fit_view_y_all);
}