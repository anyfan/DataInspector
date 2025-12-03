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

#include <QFile>
#include <QFileInfo>
#include <QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QApplication>

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
    DataManager *dm = m_mainWin->getDataManager();
    auto connSuccess = QObject::connect(m_mainWin, &MainWindow::dataProcessingFinished, [&](const QString &filePath)
                                        {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = true;
            loop.quit();
        } });
    auto connFail = QObject::connect(dm, &DataManager::loadFailed, [&](const QString &filePath, const QString &err)
                                     {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = false;
            errorMessage = err;
            loop.quit();
        } });
    m_mainWin->loadFile(qPath);
    loop.exec();
    QObject::disconnect(connSuccess);
    QObject::disconnect(connFail);
    if (!success)
        log("File load failed: " + errorMessage.toStdString());
    return success;
}

bool ScriptAPI::remove_file(std::string filename)
{
    if (!m_mainWin)
        return false;
    m_mainWin->removeFile(QString::fromStdString(filename));
    return true;
}

void ScriptAPI::import_view(std::string path)
{
    if (!m_mainWin)
        return;
    QString qPath = QString::fromStdString(path);
    QEventLoop loop;
    QObject::connect(m_mainWin, &MainWindow::viewImportFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, m_mainWin, [this, qPath]()
                       { m_mainWin->importView(qPath); });
    loop.exec();
}

std::string ScriptAPI::find_id(std::string name)
{
    if (!m_mainWin || !m_mainWin->m_signalBrowser)
        return "";
    QStandardItem *item = m_mainWin->m_signalBrowser->findItemByName(QString::fromStdString(name));
    if (item)
        return item->data(TreeItemRoles::UniqueIdRole).toString().toStdString();
    log("Signal name not found: " + name);
    return "";
}

void ScriptAPI::fit_view_y_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, [=]()
                       { m_mainWin->getPlotManager()->performFitView(false, true, PlotManager::FitAllPlots); });
    loop.exec();
}

void ScriptAPI::fit_view_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, [=]()
                       { m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitAllPlots); });
    loop.exec();
}

std::vector<double> ScriptAPI::get_data(std::string id)
{
    if (!m_mainWin)
        return {};
    SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
    if (loc.table && loc.signalIndex >= 0 && loc.signalIndex < loc.table->valueData.size())
    {
        const QVector<double> &qvec = loc.table->valueData[loc.signalIndex];
        return std::vector<double>(qvec.begin(), qvec.end());
    }
    return {};
}

std::vector<double> ScriptAPI::get_time_data(std::string id)
{
    if (!m_mainWin)
        return {};
    SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
    if (loc.table)
    {
        const QVector<double> &qvec = loc.table->timeData;
        return std::vector<double>(qvec.begin(), qvec.end());
    }
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
    if (view_index < 0)
        return m_mainWin->getPlotManager()->getActivePlot();
    auto &plots = m_mainWin->getPlotManager()->getPlots();
    if (view_index >= 0 && view_index < plots.size())
        return plots[view_index];
    return nullptr;
}

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

void ScriptAPI::set_layout(int rows, int cols)
{
    if (m_mainWin && m_mainWin->getPlotManager())
    {
        QEventLoop loop;
        QObject::connect(m_mainWin->getPlotManager(), &PlotManager::layoutChanged, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, [=]()
                           { m_mainWin->getPlotManager()->setupLayout(rows, cols); });
        loop.exec();
    }
}

int ScriptAPI::get_view_count()
{
    if (m_mainWin && m_mainWin->getPlotManager())
        return m_mainWin->getPlotManager()->getPlotCount();
    return 0;
}

int ScriptAPI::get_active_view_index()
{
    if (m_mainWin && m_mainWin->getPlotManager())
        return m_mainWin->getPlotManager()->getActivePlotIndex();
    return -1;
}

void ScriptAPI::set_active_view(int index)
{
    if (m_mainWin && m_mainWin->getPlotManager())
        m_mainWin->getPlotManager()->setActivePlotIndex(index);
}

void ScriptAPI::set_x_range(double min, double max, int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        QEventLoop loop;
        QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, [=]()
                           {
            plot->xAxis->setRange(min, max);
            plot->replot(); });
        loop.exec();
    }
}

void ScriptAPI::set_y_range(double min, double max, int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
    {
        QEventLoop loop;
        QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, [=]()
                           {
            plot->yAxis->setRange(min, max);
            plot->replot(); });
        loop.exec();
    }
}

std::tuple<double, double> ScriptAPI::get_x_range(int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
        return std::make_tuple(plot->xAxis->range().lower, plot->xAxis->range().upper);
    return std::make_tuple(0.0, 1.0);
}

std::tuple<double, double> ScriptAPI::get_y_range(int view_index)
{
    QCustomPlot *plot = getTargetPlot(view_index);
    if (plot)
        return std::make_tuple(plot->yAxis->range().lower, plot->yAxis->range().upper);
    return std::make_tuple(0.0, 1.0);
}

void ScriptAPI::autoscale(int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);
    QTimer::singleShot(0, [=]()
                       {
        if (view_index < 0)
            m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitActivePlot);
        else {
            QCustomPlot *plot = getTargetPlot(view_index);
            if (plot) {
                QList<QCustomPlot *> targets;
                targets << plot;
                m_mainWin->getPlotManager()->performFitView(targets, true, true);
            }
        } });
    loop.exec();
}

std::vector<std::string> ScriptAPI::get_view_signals(int view_index)
{
    std::vector<std::string> result;
    if (!m_mainWin || !m_mainWin->getPlotManager())
        return result;
    int idx = view_index < 0 ? get_active_view_index() : view_index;
    QSet<QString> ids = m_mainWin->getPlotManager()->getPlotSignalIDs(idx);
    for (const QString &s : ids)
        result.push_back(s.toStdString());
    return result;
}

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
        m_mainWin->getPlotManager()->addSignal(qid, loc, plot);
        if (plot == m_mainWin->getPlotManager()->getActivePlot())
            m_mainWin->m_signalBrowser->setSignalChecked(qid, true, true);
        return true;
    }
    return false;
}

bool ScriptAPI::remove_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;
    QCustomPlot *plot = getTargetPlot(view_index);
    if (!plot)
        return false;
    QString qid = QString::fromStdString(id);
    m_mainWin->getPlotManager()->removeSignal(qid, plot);
    if (plot == m_mainWin->getPlotManager()->getActivePlot())
        m_mainWin->m_signalBrowser->setSignalChecked(qid, false, true);
    return true;
}

std::string ScriptAPI::get_signal_name(std::string id)
{
    if (!m_mainWin || !m_mainWin->m_signalBrowser)
        return "";
    return m_mainWin->m_signalBrowser->getSignalName(QString::fromStdString(id)).toStdString();
}

bool ScriptAPI::export_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return m_mainWin->exportViewToJson(QString::fromStdString(path));
}

bool ScriptAPI::import_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return m_mainWin->importViewFromJson(QString::fromStdString(path));
}

static uint16_t do_crc_R_calculate(const uint8_t *data, size_t len)
{
    uint16_t crc_reg = 0xffff;
    for (size_t i = 0; i < len; ++i)
    {
        uint8_t index = (crc_reg ^ data[i]) & 0xff;
        uint16_t to_xor = index;
        for (int j = 0; j < 8; ++j)
        {
            if (to_xor & 0x0001)
            {
                to_xor = (to_xor >> 1) ^ 0x8408;
            }
            else
            {
                to_xor >>= 1;
            }
        }
        crc_reg = (crc_reg >> 8) ^ to_xor;
    }
    return (crc_reg ^ 0xffff);
}

struct ScanChunk
{
    const uint8_t *dataStart;
    size_t totalSize;
    size_t startOffset;
    size_t endOffset;
    bool isOldRec;
};

struct ChunkResult
{
    std::vector<std::pair<int, std::string>> packets;
    int errorCount = 0;
};

// Worker 函数
static ChunkResult scan_chunk_worker(const ScanChunk &chunk)
{
    ChunkResult res;
    // 预分配内存
    res.packets.reserve((chunk.endOffset - chunk.startOffset) / 50);

    size_t offset = chunk.startOffset;
    const uint8_t *buffer = chunk.dataStart;

    while (offset + 4 < chunk.totalSize)
    {
        if (offset >= chunk.endOffset)
            break; // 超出本块范围

        if (buffer[offset] != 0xEB || buffer[offset + 1] != 0x90)
        {
            offset++;
            continue;
        }

        uint8_t b0 = buffer[offset + 2];
        uint8_t b1 = buffer[offset + 3];

        int packet_id = 0;
        int packet_len = 0;

        if (chunk.isOldRec)
        {
            packet_id = (b0 & 0x7f);
            packet_len = ((b0 & 0x80) << 1) | b1;
        }
        else
        {
            packet_id = (b0 & 0x1f);
            packet_len = ((b0 & 0xE0) << 3) | b1;
        }

        size_t payload_start = offset + 4;
        size_t crc_pos = payload_start + packet_len;

        if (crc_pos + 2 > chunk.totalSize)
            break;

        uint16_t file_crc = buffer[crc_pos] | (buffer[crc_pos + 1] << 8);
        uint16_t calc_crc = do_crc_R_calculate(&buffer[payload_start], packet_len);

        if (file_crc == calc_crc)
        {
            // 校验通过
            std::string payload(reinterpret_cast<const char *>(&buffer[payload_start]), packet_len);
            res.packets.emplace_back(packet_id, std::move(payload));
            offset = crc_pos + 2;
        }
        else
        {
            // 校验失败，记录错误并继续寻找
            res.errorCount++;
            offset += 2;
        }
    }
    return res;
}

// 主解析函数
py::tuple ScriptAPI::parse_flight_data_fast(std::string path, std::string protocol)
{
    // 1. 读取文件
    QString qPath = QString::fromStdString(path);
    QFile file(qPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log("Error: Could not open file " + path);
        // 返回空元组
        return py::make_tuple(py::list(), py::dict());
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty())
    {
        return py::make_tuple(py::list(), py::dict());
    }

    // 2. 准备分块并发
    int threadCount = QThread::idealThreadCount();
    if (threadCount < 1)
        threadCount = 1;
    if (data.size() < 200000)
        threadCount = 1;

    std::vector<ScanChunk> chunks;
    size_t chunkSize = data.size() / threadCount;
    const uint8_t *rawData = reinterpret_cast<const uint8_t *>(data.constData());
    bool isOld = (protocol == "old_rec");

    for (int i = 0; i < threadCount; ++i)
    {
        ScanChunk chunk;
        chunk.dataStart = rawData;
        chunk.totalSize = data.size();
        chunk.startOffset = i * chunkSize;
        chunk.endOffset = (i == threadCount - 1) ? data.size() : (i + 1) * chunkSize;
        chunk.isOldRec = isOld;
        chunks.push_back(chunk);
    }

    // 3. 启动后台计算 (使用 QtConcurrent)
    QFuture<ChunkResult> future = QtConcurrent::mapped(chunks, scan_chunk_worker);

    QFutureWatcher<ChunkResult> watcher;
    watcher.setFuture(future);

    // 4. 事件循环 (防止界面冻结)
    QEventLoop loop;
    // 当所有线程完成时退出循环
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    // 阻塞在这里，但 QEventLoop 会继续处理 GUI 事件（保持响应）
    loop.exec();

    // 5. 合并结果
    std::vector<std::pair<int, py::bytes>> allPackets;
    size_t totalErrors = 0;
    size_t totalValid = 0;

    // 预分配
    size_t estimatedTotal = 0;
    for (const auto &res : future.results())
    {
        estimatedTotal += res.packets.size();
    }
    allPackets.reserve(estimatedTotal);

    for (const auto &res : future.results())
    {
        totalErrors += res.errorCount;
        totalValid += res.packets.size();
        for (const auto &pair : res.packets)
        {
            // 转为 py::bytes
            allPackets.emplace_back(pair.first, py::bytes(pair.second));
        }
    }

    // 6. 构造统计字典
    py::dict stats;
    stats["valid"] = totalValid;
    stats["error"] = totalErrors;

    // 7. 返回元组 (packets, stats)
    // std::vector 会自动转换为 Python list
    return py::make_tuple(allPackets, stats);
}

PYBIND11_EMBEDDED_MODULE(inspector, m)
{
    py::class_<ScriptAPI>(m, "API")
        .def(py::init<MainWindow *>())
        .def("log", &ScriptAPI::log)
        .def("load_file", &ScriptAPI::load_file, py::arg("path"), py::arg("overwrite") = false)
        .def("remove_file", &ScriptAPI::remove_file)
        .def("import_view", &ScriptAPI::import_view)
        .def("export_plot", &ScriptAPI::export_plot)
        .def("export_view", &ScriptAPI::export_view)
        .def("export_view_json", &ScriptAPI::export_view_json)
        .def("import_view_json", &ScriptAPI::import_view_json)
        .def("find_id", &ScriptAPI::find_id)
        .def("get_data", &ScriptAPI::get_data)
        .def("get_time_data", &ScriptAPI::get_time_data)
        .def("get_all_signal_ids", &ScriptAPI::get_all_signal_ids)
        .def("get_signal_name", &ScriptAPI::get_signal_name, py::arg("id"))
        .def("set_layout", &ScriptAPI::set_layout, py::arg("rows"), py::arg("cols"))
        .def("get_view_count", &ScriptAPI::get_view_count)
        .def("get_active_view_index", &ScriptAPI::get_active_view_index)
        .def("set_active_view", &ScriptAPI::set_active_view, py::arg("index"))
        .def("get_view_signals", &ScriptAPI::get_view_signals, py::arg("view_index") = -1)
        .def("add_signal", &ScriptAPI::add_signal, py::arg("id"), py::arg("view_index") = -1)
        .def("remove_signal", &ScriptAPI::remove_signal, py::arg("id"), py::arg("view_index") = -1)
        .def("set_x_range", &ScriptAPI::set_x_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("set_y_range", &ScriptAPI::set_y_range, py::arg("min"), py::arg("max"), py::arg("view_index") = -1)
        .def("get_x_range", &ScriptAPI::get_x_range, py::arg("view_index") = -1)
        .def("get_y_range", &ScriptAPI::get_y_range, py::arg("view_index") = -1)
        .def("autoscale", &ScriptAPI::autoscale, py::arg("view_index") = -1)
        .def("fit_view_y_all", &ScriptAPI::fit_view_y_all)
        .def("fit_view_all", &ScriptAPI::fit_view_all)
        // [更新接口定义]
        .def("parse_flight_data_fast", &ScriptAPI::parse_flight_data_fast,
             "C++ Accelerated parsing: returns (packets_list, stats_dict)",
             py::arg("path"), py::arg("protocol"));
}
