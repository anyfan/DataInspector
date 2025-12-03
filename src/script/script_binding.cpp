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
#include <QMetaObject>
#include <QThread>

namespace py = pybind11;

// --- 线程安全辅助函数 ---

// 在主线程执行并返回结果 (阻塞等待)
template <typename Func>
auto runOnMain(MainWindow *win, Func &&f) -> decltype(f())
{
    using R = decltype(f());
    // 如果已经在主线程，直接执行
    if (QThread::currentThread() == win->thread())
    {
        return f();
    }
    // 否则调度到主线程
    R ret;
    QMetaObject::invokeMethod(win, [&]()
                              { ret = f(); }, Qt::BlockingQueuedConnection);
    return ret;
}

// 在主线程执行无返回值的函数 (阻塞等待)
template <typename Func>
void runOnMainVoid(MainWindow *win, Func &&f)
{
    if (QThread::currentThread() == win->thread())
    {
        f();
        return;
    }
    QMetaObject::invokeMethod(win, f, Qt::BlockingQueuedConnection);
}

// ----------------------

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

    // 1. 预处理 (线程安全，无需主线程)
    QString qPath = QString::fromStdString(path);
    QString cleanPath = QFileInfo(qPath).absoluteFilePath();
    QString fileName = QFileInfo(cleanPath).fileName();

    // 2. 检查文件是否存在和移除 (需主线程)
    bool exists = runOnMain(m_mainWin, [&]()
                            { return m_mainWin->m_fileDataMap.contains(fileName); });

    if (overwrite && exists)
    {
        runOnMainVoid(m_mainWin, [&]()
                      { m_mainWin->removeFile(fileName); });
        log("Overwriting file: " + fileName.toStdString());
    }

    // 3. 启动加载
    QEventLoop loop;
    bool success = false;
    QString errorMessage;
    DataManager *dm = m_mainWin->getDataManager();

    // 连接信号 (connect本身是线程安全的)
    auto connSuccess = QObject::connect(m_mainWin, &MainWindow::dataProcessingFinished, [&](const QString &filePath)
                                        {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = true;
            loop.quit();
        } });

    // 注意：loadFailed 信号可能来自 DataManager 线程，连接到这里的 lambda 会在 emit 线程执行，或者 loop 所在线程执行
    auto connFail = QObject::connect(dm, &DataManager::loadFailed, [&](const QString &filePath, const QString &err)
                                     {
        if (QFileInfo(filePath).absoluteFilePath() == cleanPath) {
            success = false;
            errorMessage = err;
            loop.quit();
        } });

    // 触发加载 (需主线程，因为 loadFile 操作 UI)
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->loadFile(qPath); });

    // 4. 等待 (loop.exec 会阻塞当前的工作线程，直到主线程或其他线程 emit quit)
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
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->removeFile(QString::fromStdString(filename)); });
    return true;
}

void ScriptAPI::import_view(std::string path)
{
    if (!m_mainWin)
        return;
    QString qPath = QString::fromStdString(path);

    QEventLoop loop;
    QObject::connect(m_mainWin, &MainWindow::viewImportFinished, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->importView(qPath); });

    loop.exec();
}

std::string ScriptAPI::find_id(std::string name)
{
    if (!m_mainWin)
        return "";
    return runOnMain(m_mainWin, [&]() -> std::string
                     {
        if (!m_mainWin->m_signalBrowser) return "";
        QStandardItem *item = m_mainWin->m_signalBrowser->findItemByName(QString::fromStdString(name));
        if (item)
            return item->data(TreeItemRoles::UniqueIdRole).toString().toStdString();
        return ""; });
}

void ScriptAPI::fit_view_y_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->performFitView(false, true, PlotManager::FitAllPlots); });
    loop.exec();
}

void ScriptAPI::fit_view_all()
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->performFitView(true, true, PlotManager::FitAllPlots); });
    loop.exec();
}

std::vector<double> ScriptAPI::get_data(std::string id)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
        if (loc.table && loc.signalIndex >= 0 && loc.signalIndex < loc.table->valueData.size())
        {
            const QVector<double> &qvec = loc.table->valueData[loc.signalIndex];
            return std::vector<double>(qvec.begin(), qvec.end());
        }
        return std::vector<double>(); });
}

std::vector<double> ScriptAPI::get_time_data(std::string id)
{
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        SignalLocation loc = m_mainWin->getSignalDataFromID(QString::fromStdString(id));
        if (loc.table)
        {
            const QVector<double> &qvec = loc.table->timeData;
            return std::vector<double>(qvec.begin(), qvec.end());
        }
        return std::vector<double>(); });
}

bool ScriptAPI::export_plot(std::string path)
{
    if (!m_mainWin)
        return false;
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->exportActivePlot(QString::fromStdString(path)); });
    return true;
}

bool ScriptAPI::export_view(std::string path)
{
    if (!m_mainWin)
        return false;
    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->exportAllViews(QString::fromStdString(path)); });
    return true;
}

QCustomPlot *ScriptAPI::getTargetPlot(int view_index)
{
    // 这是一个私有辅助函数，假设已经在主线程上下文中被调用，或者调用者处理了线程安全。
    // 但在当前设计中，只有被 runOnMain 包裹的 lambda 才会调用它。
    // 为了安全，最好不要直接暴露给 Python，或者在内部检查。
    // 这里的实现依赖于外部 lambda 在主线程运行。
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
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::vector<std::string> result;
        if (m_mainWin->m_signalBrowser)
        {
            QStringList ids = m_mainWin->m_signalBrowser->getAllSignalIDs();
            for (const QString &s : ids)
                result.push_back(s.toStdString());
        }
        return result; });
}

void ScriptAPI::set_layout(int rows, int cols)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::layoutChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  { m_mainWin->getPlotManager()->setupLayout(rows, cols); });
    loop.exec();
}

int ScriptAPI::get_view_count()
{
    if (!m_mainWin)
        return 0;
    return runOnMain(m_mainWin, [&]()
                     {
        if (m_mainWin->getPlotManager())
            return m_mainWin->getPlotManager()->getPlotCount();
        return 0; });
}

int ScriptAPI::get_active_view_index()
{
    if (!m_mainWin)
        return -1;
    return runOnMain(m_mainWin, [&]()
                     {
        if (m_mainWin->getPlotManager())
            return m_mainWin->getPlotManager()->getActivePlotIndex();
        return -1; });
}

void ScriptAPI::set_active_view(int index)
{
    if (!m_mainWin)
        return;
    runOnMainVoid(m_mainWin, [&]()
                  {
        if (m_mainWin->getPlotManager())
            m_mainWin->getPlotManager()->setActivePlotIndex(index); });
}

void ScriptAPI::set_x_range(double min, double max, int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot) {
            plot->xAxis->setRange(min, max);
            plot->replot();
        } });
    loop.exec();
}

void ScriptAPI::set_y_range(double min, double max, int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
                  {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot) {
            plot->yAxis->setRange(min, max);
            plot->replot();
        } });
    loop.exec();
}

std::tuple<double, double> ScriptAPI::get_x_range(int view_index)
{
    if (!m_mainWin)
        return std::make_tuple(0.0, 1.0);
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot)
            return std::make_tuple(plot->xAxis->range().lower, plot->xAxis->range().upper);
        return std::make_tuple(0.0, 1.0); });
}

std::tuple<double, double> ScriptAPI::get_y_range(int view_index)
{
    if (!m_mainWin)
        return std::make_tuple(0.0, 1.0);
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (plot)
            return std::make_tuple(plot->yAxis->range().lower, plot->yAxis->range().upper);
        return std::make_tuple(0.0, 1.0); });
}

void ScriptAPI::autoscale(int view_index)
{
    if (!m_mainWin)
        return;
    QEventLoop loop;
    QObject::connect(m_mainWin->getPlotManager(), &PlotManager::viewChanged, &loop, &QEventLoop::quit);

    runOnMainVoid(m_mainWin, [&]()
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
    if (!m_mainWin)
        return {};
    return runOnMain(m_mainWin, [&]()
                     {
        std::vector<std::string> result;
        if (!m_mainWin->getPlotManager()) return result;
        int idx = view_index < 0 ? m_mainWin->getPlotManager()->getActivePlotIndex() : view_index;
        QSet<QString> ids = m_mainWin->getPlotManager()->getPlotSignalIDs(idx);
        for (const QString &s : ids)
            result.push_back(s.toStdString());
        return result; });
}

bool ScriptAPI::add_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (!plot) return false;
        QString qid = QString::fromStdString(id);
        SignalLocation loc = m_mainWin->getSignalDataFromID(qid);
        if (loc.table)
        {
            m_mainWin->getPlotManager()->addSignal(qid, loc, plot);
            if (plot == m_mainWin->getPlotManager()->getActivePlot())
                m_mainWin->m_signalBrowser->setSignalChecked(qid, true, true);
            return true;
        }
        return false; });
}

bool ScriptAPI::remove_signal(std::string id, int view_index)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     {
        QCustomPlot *plot = getTargetPlot(view_index);
        if (!plot) return false;
        QString qid = QString::fromStdString(id);
        m_mainWin->getPlotManager()->removeSignal(qid, plot);
        if (plot == m_mainWin->getPlotManager()->getActivePlot())
            m_mainWin->m_signalBrowser->setSignalChecked(qid, false, true);
        return true; });
}

std::string ScriptAPI::get_signal_name(std::string id)
{
    if (!m_mainWin)
        return "";
    return runOnMain(m_mainWin, [&]()
                     {
        if (!m_mainWin->m_signalBrowser) return std::string("");
        return m_mainWin->m_signalBrowser->getSignalName(QString::fromStdString(id)).toStdString(); });
}

bool ScriptAPI::export_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     { return m_mainWin->exportViewToJson(QString::fromStdString(path)); });
}

bool ScriptAPI::import_view_json(std::string path)
{
    if (!m_mainWin)
        return false;
    return runOnMain(m_mainWin, [&]()
                     { return m_mainWin->importViewFromJson(QString::fromStdString(path)); });
}

// ... CRC 计算和 parse_flight_data_fast 的其他部分 ...
// 注意：parse_flight_data_fast 主要是计算密集型，且使用了独立的 QtConcurrent。
// 它不涉及 UI 操作，所以可以在工作线程中直接运行，无需 runOnMain。
// 唯一的依赖是文件读取，这是线程安全的。
// 我们只需要保留原样的 CRC 和 parse_flight_data_fast 实现即可。

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

static ChunkResult scan_chunk_worker(const ScanChunk &chunk)
{
    ChunkResult res;
    res.packets.reserve((chunk.endOffset - chunk.startOffset) / 50);

    size_t offset = chunk.startOffset;
    const uint8_t *buffer = chunk.dataStart;

    while (offset + 4 < chunk.totalSize)
    {
        if (offset >= chunk.endOffset)
            break;

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
            std::string payload(reinterpret_cast<const char *>(&buffer[payload_start]), packet_len);
            res.packets.emplace_back(packet_id, std::move(payload));
            offset = crc_pos + 2;
        }
        else
        {
            res.errorCount++;
            offset += 2;
        }
    }
    return res;
}

py::tuple ScriptAPI::parse_flight_data_fast(std::string path, std::string protocol)
{
    // 该函数不操作 UI，直接在 Worker 线程运行是安全的
    QString qPath = QString::fromStdString(path);
    QFile file(qPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        log("Error: Could not open file " + path);
        return py::make_tuple(py::list(), py::dict());
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.isEmpty())
    {
        return py::make_tuple(py::list(), py::dict());
    }

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

    QFuture<ChunkResult> future = QtConcurrent::mapped(chunks, scan_chunk_worker);

    QFutureWatcher<ChunkResult> watcher;
    watcher.setFuture(future);

    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
    loop.exec(); // 等待计算完成

    std::vector<std::pair<int, py::bytes>> allPackets;
    size_t totalErrors = 0;
    size_t totalValid = 0;

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
            allPackets.emplace_back(pair.first, py::bytes(pair.second));
        }
    }

    py::dict stats;
    stats["valid"] = totalValid;
    stats["error"] = totalErrors;

    return py::make_tuple(allPackets, stats);
}

static QStringList pyListToQStringList(const py::list &list)
{
    QStringList result;
    for (auto item : list)
    {
        result.append(QString::fromStdString(py::str(item)));
    }
    return result;
}

bool ScriptAPI::load_parsed_data(std::string filename, py::dict data_dict)
{
    if (!m_mainWin)
        return false;

    FileData fileData;
    fileData.filePath = QString::fromStdString(filename);

    // 遍历字典 keys
    py::list keys = data_dict.attr("keys")();
    QRegularExpression pVarRegex("^p(\\d+)$"); // 匹配 p1, p2, p998...

    std::map<int, std::string> validTables;

    for (auto key : keys)
    {
        std::string keyStr = py::str(key);
        QString qKey = QString::fromStdString(keyStr);
        auto match = pVarRegex.match(qKey);
        if (match.hasMatch())
        {
            validTables[match.captured(1).toInt()] = keyStr;
        }
    }

    if (validTables.empty())
    {
        log("Error: No valid data tables (keys like 'p1', 'p2') found in dictionary.");
        return false;
    }

    // 遍历排序后的表
    for (auto const &[idx, pKey] : validTables)
    {
        SignalTable table;
        table.name = QString::fromStdString(pKey);

        if (!data_dict.contains(pKey))
            continue;
        py::list rows = data_dict[pKey.c_str()].cast<py::list>();

        size_t rowCount = rows.size();
        if (rowCount == 0)
            continue;

        // 获取第一行以确定列数
        py::list firstRow = rows[0].cast<py::list>();
        size_t colCount = firstRow.size(); // Time + Values
        if (colCount < 2)
            continue; // 至少要有时间和一列数据

        size_t valueColCount = colCount - 1;

        // 2. 获取标题 (Headers)
        std::string titleKey = pKey + "_title";   // 描述: "刹车指令(MPa)"
        std::string title2Key = pKey + "_title2"; // 变量名: "brake_c"

        QStringList titles, titles2;
        if (data_dict.contains(titleKey.c_str()))
            titles = pyListToQStringList(data_dict[titleKey.c_str()].cast<py::list>());
        if (data_dict.contains(title2Key.c_str()))
            titles2 = pyListToQStringList(data_dict[title2Key.c_str()].cast<py::list>());

        if (!titles.isEmpty() && titles.size() == colCount)
            titles.removeFirst();
        if (!titles2.isEmpty() && titles2.size() == colCount)
            titles2.removeFirst();

        // 组合标题: "变量名 描述"
        for (size_t i = 0; i < valueColCount; ++i)
        {
            QString header;
            if (i < titles2.size())
                header += titles2[i];
            if (i < titles.size())
            {
                if (!header.isEmpty())
                    header += " ";
                header += titles[i];
            }
            if (header.isEmpty())
                header = QString("Signal %1").arg(i + 1);
            table.headers.append(header);
        }

        table.timeData.reserve(rowCount);
        table.valueData.resize(valueColCount);
        for (auto &vec : table.valueData)
            vec.reserve(rowCount);

        try
        {
            for (auto rowItem : rows)
            {
                py::list row = rowItem.cast<py::list>();

                // 第一列是时间
                double t = row[0].cast<double>();
                table.timeData.append(t);

                // 后续是数值
                for (size_t c = 0; c < valueColCount; ++c)
                {
                    double v = row[c + 1].cast<double>();
                    table.valueData[c].append(v);
                }
            }
        }
        catch (const std::exception &e)
        {
            log(std::string("Error parsing data row: ") + e.what());
            continue;
        }

        fileData.tables.append(table);
    }

    if (fileData.tables.isEmpty())
        return false;

    // 4. 推送给 DataManager
    m_mainWin->getDataManager()->importExternalData(fileData);
    return true;
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
        .def("parse_flight_data_fast", &ScriptAPI::parse_flight_data_fast,
             "C++ Accelerated parsing: returns (packets_list, stats_dict)",
             py::arg("path"), py::arg("protocol"))
        .def("load_parsed_data", &ScriptAPI::load_parsed_data,
             "直接加载解析后的数据字典 (无需保存文件)",
             py::arg("filename"), py::arg("data_dict"));
}
