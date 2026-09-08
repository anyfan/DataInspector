#include "appcontroller.h"
#include "plotitem.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <utility>

#ifdef ENABLE_MAT
#include "matio.h"
#endif

namespace {

#ifdef ENABLE_MAT
static int utf8CharLength(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

static QStringList readMatStrings(matvar_t *variable)
{
    QStringList result;
    if (!variable || !variable->data || variable->rank != 2) return result;
    const size_t rows = variable->dims[0], cols = variable->dims[1];
    if (!rows || !cols) return result;
    if (variable->data_type == MAT_T_UTF8) {
        QVector<QByteArray> buffers(static_cast<int>(rows));
        auto *scanner = static_cast<unsigned char *>(variable->data);
        for (size_t col = 0; col < cols; ++col) for (size_t row = 0; row < rows; ++row) {
            const int length = utf8CharLength(*scanner);
            buffers[static_cast<int>(row)].append(reinterpret_cast<const char *>(scanner), length);
            scanner += length;
        }
        for (const QByteArray &buffer : buffers) result.append(QString::fromUtf8(buffer).trimmed());
    } else if (variable->data_type == MAT_T_INT8 || variable->data_type == MAT_T_UINT8) {
        const auto *data = static_cast<const char *>(variable->data);
        for (size_t row = 0; row < rows; ++row) {
            QByteArray buffer;
            for (size_t col = 0; col < cols; ++col) { const char c = data[col * rows + row]; if (!c) break; buffer.append(c); }
            result.append(QString::fromLocal8Bit(buffer).trimmed());
        }
    }
    return result;
}

static bool buildMatTable(int index, const QMap<QString, matvar_t *> &variables, LoadedTable &table)
{
    const QString name = QStringLiteral("p%1").arg(index);
    matvar_t *dataVar = variables.value(name, nullptr);
    if (!dataVar || dataVar->data_type != MAT_T_DOUBLE || dataVar->rank != 2 || dataVar->dims[1] < 2) return false;
    const size_t rows = dataVar->dims[0], cols = dataVar->dims[1];
    const int signalCount = static_cast<int>(cols - 1);
    const QStringList title1 = readMatStrings(variables.value(name + QStringLiteral("_title"), nullptr));
    const QStringList title2 = readMatStrings(variables.value(name + QStringLiteral("_title2"), nullptr));
    table.name = name;
    table.signalNames.reserve(signalCount);
    for (int i = 0; i < signalCount; ++i) {
        QString n;
        if (title2.size() == signalCount) n += title2.at(i);
        if (title1.size() == signalCount) { if (!n.isEmpty()) n += QLatin1Char(' '); n += title1.at(i); }
        table.signalNames.append(n.isEmpty() ? QStringLiteral("%1_Sig%2").arg(name).arg(i + 1) : n);
    }
    const auto *raw = static_cast<const double *>(dataVar->data);
    table.time.resize(static_cast<int>(rows));
    for (size_t row = 0; row < rows; ++row) table.time[static_cast<int>(row)] = raw[row];
    table.values.resize(signalCount);
    for (int signal = 0; signal < signalCount; ++signal) {
        table.values[signal].resize(static_cast<int>(rows));
        const double *column = raw + (signal + 1) * rows;
        std::copy(column, column + rows, table.values[signal].begin());
    }
    return true;
}
#endif

class DataLoadWorker final : public QObject
{
    Q_OBJECT
public slots:
    void loadFile(const QString &path)
    {
        if (path.endsWith(QStringLiteral(".mat"), Qt::CaseInsensitive)) {
#ifdef ENABLE_MAT
            loadMat(path);
#else
            emit finished(path, {}, 0, QStringLiteral("当前构建未启用 MAT 支持，请使用 -DENABLE_MAT=ON 并提供兼容 LLVM-MinGW 的 matio/HDF5 库"));
#endif
            return;
        }
        loadCsv(path);
    }
signals:
    void finished(const QString &path, const QVector<LoadedTable> &tables, int skipped, const QString &error);
private:
    void loadCsv(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { emit finished(path, {}, 0, QStringLiteral("无法打开文件：%1").arg(path)); return; }
        QTextStream stream(&file);
        if (stream.atEnd()) { emit finished(path, {}, 0, QStringLiteral("文件为空：%1").arg(path)); return; }
        const QStringList headers = stream.readLine().split(',');
        if (headers.size() < 2) { emit finished(path, {}, 0, QStringLiteral("CSV 至少需要时间列和一个信号列")); return; }
        LoadedTable table; table.name = QFileInfo(path).completeBaseName(); table.values.resize(headers.size() - 1);
        for (int i = 1; i < headers.size(); ++i) { const QString n = headers.at(i).trimmed(); table.signalNames.append(n.isEmpty() ? QStringLiteral("Signal %1").arg(i) : n); }
        int skipped = 0;
        while (!stream.atEnd()) {
            if (QThread::currentThread()->isInterruptionRequested()) return;
            const QString line = stream.readLine().trimmed(); if (line.isEmpty()) continue;
            const QStringList fields = line.split(','); if (fields.size() != headers.size()) { ++skipped; continue; }
            bool timeOk = false; const double timestamp = fields.first().toDouble(&timeOk); if (!timeOk) { ++skipped; continue; }
            table.time.append(timestamp);
            for (int i = 1; i < fields.size(); ++i) { bool ok = false; const double value = fields.at(i).toDouble(&ok); table.values[i-1].append(ok ? value : qQNaN()); }
        }
        if (table.time.isEmpty()) { emit finished(path, {}, skipped, QStringLiteral("没有读取到有效数据：%1").arg(path)); return; }
        emit finished(path, {table}, skipped, {});
    }
 #ifdef ENABLE_MAT
    void loadMat(const QString &path)
    {
        const QByteArray encoded = QFile::encodeName(path);
        mat_t *file = Mat_Open(encoded.constData(), MAT_ACC_RDONLY);
        if (!file) { emit finished(path, {}, 0, QStringLiteral("无法打开 MAT 文件：%1").arg(path)); return; }
        QMap<QString, matvar_t *> variables;
        QRegularExpression pExpression(QStringLiteral("^p\\d+$"));
        QList<int> indices;
        matvar_t *variable = nullptr;
        while ((variable = Mat_VarReadNext(file)) != nullptr) {
            const QString name = QString::fromLatin1(variable->name ? variable->name : "");
            if (pExpression.match(name).hasMatch() || name.endsWith(QStringLiteral("_title")) || name.endsWith(QStringLiteral("_title2"))) {
                variables.insert(name, variable);
                if (pExpression.match(name).hasMatch()) indices.append(name.mid(1).toInt());
            } else Mat_VarFree(variable);
        }
        std::sort(indices.begin(), indices.end());
        QVector<LoadedTable> tables;
        for (int index : indices) { LoadedTable table; if (buildMatTable(index, variables, table)) tables.append(std::move(table)); }
        for (matvar_t *entry : std::as_const(variables))
            Mat_VarFree(entry);
        Mat_Close(file);
        if (tables.isEmpty()) { emit finished(path, {}, 0, QStringLiteral("MAT 文件中没有有效的 pN 数据变量")); return; }
        emit finished(path, tables, 0, {});
    }
 #endif
};

} // namespace

AppController::AppController(QObject *parent) : QObject(parent), m_signals(new SignalModel(this))
{
    qRegisterMetaType<LoadedTable>();
    qRegisterMetaType<QVector<LoadedTable>>();
    m_loadThread = new QThread(this);
    auto *worker = new DataLoadWorker;
    m_loader = worker; worker->moveToThread(m_loadThread);
    connect(worker, &DataLoadWorker::finished, this, &AppController::onLoadFinished, Qt::QueuedConnection);
    connect(m_loadThread, &QThread::finished, worker, &QObject::deleteLater);
    m_loadThread->start();
}

AppController::~AppController()
{
    if (m_loadThread) { m_loadThread->requestInterruption(); m_loadThread->quit(); m_loadThread->wait(); }
}

bool AppController::loadCsv(const QString &filePath)
{
    const QString path = filePath.startsWith(QStringLiteral("file:")) ? QUrl(filePath).toLocalFile() : filePath;
    if (m_loading || !m_loader) { setStatus(QStringLiteral("正在加载文件，请稍候")); return false; }
    m_loading = true; emit loadingChanged(); setStatus(QStringLiteral("正在加载 %1…").arg(QFileInfo(path).fileName()));
    QMetaObject::invokeMethod(m_loader, "loadFile", Qt::QueuedConnection, Q_ARG(QString, path)); return true;
}

void AppController::selectSignal(int row)
{
    if (row >= 0 && row < m_enabled.size()) { m_enabled[row] = true; m_signals->setChecked(row, true); }
    if (m_plotSignals.size() < m_plots.size()) m_plotSignals.resize(m_plots.size());
    for (int p = 0; p < m_plots.size(); ++p) {
        if (m_plotSignals[p].isEmpty()) for (int i = 0; i < m_enabled.size(); ++i) if (m_enabled.value(i)) m_plotSignals[p].insert(i);
        refreshPlot(p);
    }
}

void AppController::toggleSignal(int row) { if (row < 0 || row >= m_enabled.size()) return; m_enabled[row] = !m_enabled.at(row); m_signals->setChecked(row, m_enabled.at(row)); selectSignal(-1); }
void AppController::filterSignals(const QString &text) { m_signals->setFilter(text); }
void AppController::setAllSignalsChecked(bool checked) { m_signals->setAllChecked(checked); for (int i = 0; i < m_enabled.size(); ++i) m_enabled[i] = checked; selectSignal(-1); }
void AppController::togglePlotSignal(int plotIndex, int row) { if (plotIndex < 0 || plotIndex >= m_plots.size() || row < 0 || row >= m_enabled.size()) return; if (m_plotSignals.size() <= plotIndex) m_plotSignals.resize(plotIndex + 1); if (m_plotSignals[plotIndex].contains(row)) m_plotSignals[plotIndex].remove(row); else m_plotSignals[plotIndex].insert(row); refreshPlot(plotIndex); ++m_plotStateRevision; emit plotBindingsChanged(); }
bool AppController::plotSignalEnabled(int plotIndex, int row) const { return plotIndex >= 0 && plotIndex < m_plotSignals.size() && m_plotSignals.at(plotIndex).contains(row); }
void AppController::attachPlot(QObject *plot, int index) { auto *item = qobject_cast<PlotItem *>(plot); if (!item || index < 0) return; if (index >= m_plots.size()) { m_plots.resize(index + 1); m_plotSignals.resize(index + 1); } m_plots[index] = item; connect(item, &PlotItem::rangeChanged, this, [this, item](double xmin, double xmax, double, double) { if (m_syncingRanges) return; m_syncingRanges = true; for (PlotItem *other : std::as_const(m_plots)) if (other && other != item) other->setXRange(xmin, xmax); m_syncingRanges = false; }); if (!m_tables.isEmpty()) { if (m_plotSignals[index].isEmpty()) for (int i = 0; i < m_enabled.size(); ++i) if (m_enabled.value(i)) m_plotSignals[index].insert(i); refreshPlot(index); } }
void AppController::setLayout(int rows, int columns) { m_plots.clear(); m_plotSignals.clear(); ++m_plotStateRevision; m_plotRows = qBound(1, rows, 4); m_plotColumns = qBound(1, columns, 4); emit layoutChanged(); emit plotBindingsChanged(); }
void AppController::clear() { m_tables.clear(); m_signalLocations.clear(); m_enabled.clear(); m_signalColors.clear(); m_plotSignals.clear(); m_signals->setNames({}); m_currentFile.clear(); emit currentFileChanged(); for (PlotItem *plot : std::as_const(m_plots)) if (plot) plot->clearSeries(); setStatus(QStringLiteral("已清空")); }
void AppController::setStatus(const QString &status) { if (m_status == status) return; m_status = status; emit statusChanged(); }

void AppController::onLoadFinished(const QString &path, const QVector<LoadedTable> &tables, int skipped, const QString &error)
{
    m_loading = false; emit loadingChanged(); if (!error.isEmpty()) { setStatus(error); return; }
    m_tables = tables; m_signalLocations.clear(); QStringList names;
    const bool multiple = m_tables.size() > 1;
    for (int tableIndex = 0; tableIndex < m_tables.size(); ++tableIndex) {
        const auto &table = m_tables.at(tableIndex);
        for (int signalIndex = 0; signalIndex < table.signalNames.size(); ++signalIndex) {
            names.append(multiple ? table.name + QStringLiteral(" / ") + table.signalNames.at(signalIndex) : table.signalNames.at(signalIndex));
            m_signalLocations.append({tableIndex, signalIndex});
        }
    }
    m_enabled.fill(false, names.size()); m_signalColors.resize(names.size()); for (int i = 0; i < names.size(); ++i) m_signalColors[i] = QColor::fromHsv((i * 47) % 360, 190, 230); m_plotSignals.clear(); m_plotSignals.resize(m_plots.size()); m_signals->setNames(names, m_signalColors); ++m_plotStateRevision; emit plotBindingsChanged(); m_currentFile = QFileInfo(path).fileName(); emit currentFileChanged(); selectSignal(0);
    qint64 rows = 0; for (const auto &table : m_tables) rows += table.time.size();
    setStatus(QStringLiteral("已加载 %1：%2 行，%3 个信号%4").arg(m_currentFile).arg(rows).arg(names.size()).arg(skipped ? QStringLiteral("，跳过 %1 行").arg(skipped) : QString()));
}

void AppController::refreshPlot(int index, bool fitY)
{
    if (index < 0 || index >= m_plots.size() || !m_plots.at(index)) return;
    PlotItem *plot = m_plots.at(index); plot->clearSeries();
    const QSet<int> rows = m_plotSignals.value(index);
    for (int i : rows) if (i >= 0 && i < m_signalLocations.size()) { const auto location = m_signalLocations.at(i); const auto &table = m_tables.at(location.first); plot->appendSeries(table.time, table.values.at(location.second), m_signalColors.value(i)); }
    if (fitY) plot->fitView();
}

#include "appcontroller.moc"
