#include "appcontroller.h"
#include "plotitem.h"
#include "dataloadworker.h"

#include <QFileInfo>
#include <QJSValue>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtMath>
#include <utility>

AppController::AppController(QObject *parent)
    : QObject(parent), m_signals(new SignalModel(this)),
      m_seriesStore(std::make_shared<PlotSeriesStore>())
{
    m_signals->setPlotCount(1);
    qRegisterMetaType<LoadedTable>();
    qRegisterMetaType<QVector<LoadedTable>>();
    m_loadThread = new QThread(this);
    auto *worker = new DataLoadWorker;
    m_loader = worker; worker->moveToThread(m_loadThread);
    connect(worker, &DataLoadWorker::progress, this, &AppController::onLoadProgress,
            Qt::QueuedConnection);
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
    return loadFiles(QVariant(filePath)) == 1;
}

int AppController::loadFiles(const QVariant &filePaths)
{
    if (!m_loader) return 0;
    QVariantList paths;
    if (filePaths.typeId() == qMetaTypeId<QJSValue>()) {
        const QVariant converted = filePaths.value<QJSValue>().toVariant();
        paths = converted.canConvert<QVariantList>()
            ? converted.toList() : QVariantList{converted};
    } else if (filePaths.typeId() == QMetaType::QString
        || filePaths.typeId() == QMetaType::QByteArray) {
        paths.append(filePaths);
    } else if (filePaths.canConvert<QUrl>()) {
        paths.append(filePaths);
    } else if (filePaths.canConvert<QVariantList>()) {
        paths = filePaths.toList();
    } else if (!filePaths.toString().isEmpty()) {
        paths.append(filePaths);
    }
    int accepted = 0;
    for (const QVariant &value : paths) {
        const QUrl url = value.canConvert<QUrl>() ? value.toUrl() : QUrl(value.toString());
        QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
        if (path.startsWith(QStringLiteral("file:"))) path = QUrl(path).toLocalFile();
        QFileInfo info(path);
        path = info.canonicalFilePath();
        if (path.isEmpty()) path = info.absoluteFilePath();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (path.isEmpty() || (suffix != QStringLiteral("csv")
                               && suffix != QStringLiteral("txt")
                               && suffix != QStringLiteral("mat"))) continue;
        if (m_loadedPaths.contains(path) || m_pendingPaths.contains(path)
            || m_activeLoadPath == path) continue;
        m_loadQueue.enqueue(path);
        m_pendingPaths.insert(path);
        ++accepted;
    }
    if (!accepted) {
        setStatus(QStringLiteral("没有可导入的新数据文件"));
        return 0;
    }
    if (!m_loading) {
        m_batchTotal = accepted;
        m_batchCompleted = 0;
        m_batchErrors = 0;
        m_batchSignals = 0;
        m_batchRows = 0;
        m_batchSkipped = 0;
        m_batchFirstError.clear();
        setLoadingProgress(0);
        m_loading = true;
        emit loadingChanged();
        startNextLoad();
    } else {
        m_batchTotal += accepted;
    }
    return accepted;
}

bool AppController::removeFile(const QString &fileName)
{
    if (m_loading || fileName.isEmpty()) return false;
    const QVector<int> removedRows = m_signals->removeFile(fileName);
    if (removedRows.isEmpty()) return false;

    m_seriesStore->removeSeries(QSet<int>(removedRows.cbegin(),
                                           removedRows.cend()));
    for (auto it = removedRows.crbegin(); it != removedRows.crend(); ++it)
        if (*it >= 0 && *it < m_signalColors.size())
            m_signalColors.removeAt(*it);
    for (auto it = m_loadedPaths.begin(); it != m_loadedPaths.end();) {
        if (QFileInfo(*it).fileName() == fileName)
            it = m_loadedPaths.erase(it);
        else
            ++it;
    }
    m_loadedFileNames.removeAll(fileName);
    m_currentFile = m_loadedFileNames.isEmpty()
        ? QString()
        : m_loadedFileNames.size() == 1
            ? m_loadedFileNames.first()
            : QStringLiteral("已加载 %1 个文件").arg(m_loadedFileNames.size());
    emit currentFileChanged();
    for (int index = 0; index < m_plots.size(); ++index)
        refreshPlot(index, false);
    ++m_plotStateRevision;
    emit plotBindingsChanged();
    setStatus(QStringLiteral("已移除文件：%1").arg(fileName));
    return true;
}

void AppController::selectSignal(int row)
{
    if (row >= 0) m_signals->setChecked(row, true);
    const int plotIndex = m_signals->activePlot();
    if (plotIndex >= 0) refreshPlot(plotIndex);
    ++m_plotStateRevision;
    emit plotBindingsChanged();
}

void AppController::toggleSignal(int row)
{
    const int plotIndex = m_signals->activePlot();
    if (plotIndex < 0 || row < 0) return;
    m_signals->setPlotChecked(plotIndex, row,
                              !m_signals->plotRows(plotIndex).contains(row));
    refreshPlot(plotIndex);
    ++m_plotStateRevision;
    emit plotBindingsChanged();
}
void AppController::filterSignals(const QString &text) { m_signals->setFilter(text); }
void AppController::setAllSignalsChecked(bool checked) { m_signals->setAllChecked(checked); const int plotIndex = m_signals->activePlot(); if (plotIndex >= 0) refreshPlot(plotIndex); ++m_plotStateRevision; emit plotBindingsChanged(); }
bool AppController::plotSignalEnabled(int plotIndex, int row) const { return m_signals->plotRows(plotIndex).contains(row); }
QString AppController::signalName(int row) const
{
    return m_signals->nameAt(row);
}
QVariantList AppController::plotSignalRows(int plotIndex) const
{
    QVariantList rows;
    for (int row : m_signals->plotRows(plotIndex)) rows.append(row);
    return rows;
}
void AppController::setSignalPen(int row, const QColor &color,
                                 double width, int style)
{
    if (row < 0 || row >= m_signalColors.size()) return;
    const auto penStyle = static_cast<Qt::PenStyle>(style);
    m_signals->setSignalPen(row, color, width, penStyle);
    m_signalColors[row] = m_signals->signalColor(row);
    m_seriesStore->updateSeriesPen(row, m_signals->signalColor(row),
                                   m_signals->signalWidth(row),
                                   m_signals->signalStyle(row));
    for (int plotIndex = 0; plotIndex < m_plots.size(); ++plotIndex)
        if (m_signals->plotRows(plotIndex).contains(row))
            refreshPlot(plotIndex, false);
    ++m_plotStateRevision;
    emit plotBindingsChanged();
}
void AppController::setLegendMode(int mode)
{
    const int normalized = qBound(0, mode, 3);
    if (m_legendMode == normalized) return;
    m_legendMode = normalized;
    emit legendModeChanged();
}
void AppController::attachPlot(QObject *plot, int index)
{
    auto *item = qobject_cast<PlotItem *>(plot);
    if (!item || index < 0) return;
    if (index < m_plots.size() && m_plots[index] == item) {
        item->setSeriesStore(m_seriesStore);
        refreshPlot(index);
        return;
    }
    // Retired QML delegates can remain alive until deferred destruction.
    if (index < m_plots.size() && m_plots.at(index))
        disconnect(m_plots.at(index), nullptr, this, nullptr);
    for (int previous = 0; previous < m_plots.size(); ++previous) {
        if (m_plots.at(previous) == item) {
            disconnect(item, nullptr, this, nullptr);
            m_plots[previous].clear();
        }
    }
    if (index >= m_plots.size()) m_plots.resize(index + 1);
    m_plots[index] = item;
    item->setSeriesStore(m_seriesStore);
    item->setXRange(m_sharedXMinimum, m_sharedXMaximum);
    connect(item, &PlotItem::rangeChanged, this,
            [this, item](double xmin, double xmax, double, double) {
                if (m_syncingRanges) return;
                m_sharedXMinimum = xmin;
                m_sharedXMaximum = xmax;
                m_syncingRanges = true;
                for (const QPointer<PlotItem> &other : std::as_const(m_plots))
                    if (other && other != item) other->setXRange(xmin, xmax);
                m_syncingRanges = false;
            });
    connect(item, &PlotItem::cursorChanged, this, [this, item]() {
        if (m_syncingCursors) return;
        m_syncingCursors = true;
        for (const QPointer<PlotItem> &other : std::as_const(m_plots)) {
            if (!other || other == item) continue;
            other->setCursorMode(item->cursorMode());
            if (item->cursorMode() != PlotItem::NoCursor) {
                other->setCursorPosition(item->cursorX1(), 1);
                if (item->cursorMode() == PlotItem::DoubleCursor)
                    other->setCursorPosition(item->cursorX2(), 2);
            }
        }
        m_syncingCursors = false;
    });
    refreshPlot(index);
    for (const QPointer<PlotItem> &source : std::as_const(m_plots)) {
        if (!source || source == item) continue;
        item->setCursorMode(source->cursorMode());
        if (source->cursorMode() != PlotItem::NoCursor) {
            item->setCursorPosition(source->cursorX1(), 1);
            if (source->cursorMode() == PlotItem::DoubleCursor)
                item->setCursorPosition(source->cursorX2(), 2);
        }
        break;
    }
}

void AppController::detachPlot(QObject *plot, int index)
{
    auto *item = qobject_cast<PlotItem *>(plot);
    if (!item || index < 0 || index >= m_plots.size()) return;
    if (m_plots.at(index) != item) return;
    disconnect(item, nullptr, this, nullptr);
    m_plots[index].clear();
}

void AppController::setLayout(int rows, int columns)
{
    const int normalizedRows = qBound(1, rows, 8);
    const int normalizedColumns = qBound(1, columns, 8);
    if (m_plotRows == normalizedRows && m_plotColumns == normalizedColumns)
        return;
    // A numeric QML Repeater retains delegates whose indices still exist.
    // Preserve those attachments and leave new slots empty for attachPlot().
    const int plotCount = normalizedRows * normalizedColumns;
    for (int index = plotCount; index < m_plots.size(); ++index)
        if (m_plots.at(index))
            disconnect(m_plots.at(index), nullptr, this, nullptr);
    m_plots.resize(plotCount);
    ++m_plotStateRevision;
    const int previousActivePlot = m_signals->activePlot();
    m_plotRows = normalizedRows;
    m_plotColumns = normalizedColumns;
    m_signals->setPlotCount(m_plotRows * m_plotColumns);
    if (m_signals->activePlot() != previousActivePlot) emit activePlotChanged();
    emit layoutChanged();
    emit plotBindingsChanged();
    for (int index = 0; index < m_plots.size(); ++index) refreshPlot(index, false);
    QTimer::singleShot(0, this, [this]() {
        for (int index = 0; index < m_plots.size(); ++index)
            refreshPlot(index, false);
    });
}
void AppController::setActivePlot(int index)
{
    if (index == m_signals->activePlot()) return;
    m_signals->setActivePlot(index);
    emit activePlotChanged();
}
void AppController::fitAllPlots()
{
    for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
        if (plot) plot->fitView();
}
void AppController::clear()
{
    if (m_loading) {
        setStatus(QStringLiteral("文件正在加载，完成后再清空"));
        return;
    }
    m_seriesStore->clear();
    m_signalColors.clear();
    m_signals->setNames({});
    m_loadedPaths.clear();
    m_loadedFileNames.clear();
    m_currentFile.clear();
    emit currentFileChanged();
    for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
        if (plot)
            plot->setVisibleSeries({});
    ++m_plotStateRevision;
    emit plotBindingsChanged();
    setLoadingProgress(0);
    setStatus(QStringLiteral("已清空"));
}
void AppController::setStatus(const QString &status) { if (m_status == status) return; m_status = status; emit statusChanged(); }

void AppController::setLoadingProgress(int progress)
{
    const int normalized = qBound(0, progress, 100);
    if (m_loading && normalized < m_loadingProgress) return;
    if (m_loadingProgress == normalized) return;
    m_loadingProgress = normalized;
    emit loadingProgressChanged();
}

void AppController::startNextLoad()
{
    if (m_loadQueue.isEmpty()) {
        m_activeLoadPath.clear();
        m_loading = false;
        emit loadingChanged();
        setLoadingProgress(100);
        QString summary = QStringLiteral("已加载 %1 个文件：%2 行，%3 个信号")
                              .arg(m_batchTotal - m_batchErrors)
                              .arg(m_batchRows).arg(m_batchSignals);
        if (m_batchSkipped > 0)
            summary += QStringLiteral("，跳过 %1 行").arg(m_batchSkipped);
        if (m_batchErrors > 0)
            summary += QStringLiteral("，%1 个文件失败").arg(m_batchErrors);
        if (!m_batchFirstError.isEmpty())
            summary += QStringLiteral("：%1").arg(m_batchFirstError);
        setStatus(summary);
        return;
    }
    m_activeLoadPath = m_loadQueue.dequeue();
    setStatus(QStringLiteral("正在加载 %1（%2/%3）…")
                  .arg(QFileInfo(m_activeLoadPath).fileName())
                  .arg(m_batchCompleted + 1).arg(m_batchTotal));
    QMetaObject::invokeMethod(m_loader, &DataLoadWorker::loadFile,
                              Qt::QueuedConnection, m_activeLoadPath);
}

void AppController::onLoadProgress(const QString &path, int percentage)
{
    if (!m_loading || path != m_activeLoadPath || m_batchTotal <= 0) return;
    const int aggregate = (m_batchCompleted * 100 + qBound(0, percentage, 100))
        / m_batchTotal;
    setLoadingProgress(aggregate);
}

void AppController::onLoadFinished(const QString &path, const QVector<LoadedTable> &tables, int skipped, const QString &error)
{
    m_pendingPaths.remove(path);
    if (!error.isEmpty()) {
        ++m_batchErrors;
        if (m_batchFirstError.isEmpty())
            m_batchFirstError = error;
    } else {
        QStringList names;
        QStringList groups;
        QVector<QColor> colors;
        const QString fileName = QFileInfo(path).fileName();
        const QString fileBaseName = QFileInfo(path).completeBaseName();
        for (int tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
            const auto &table = tables.at(tableIndex);
            const bool skipTableNode = tables.size() == 1
                && table.name == fileBaseName;
            const QString group = skipTableNode
                ? fileName : fileName + QLatin1Char('/') + table.name;
            for (int signalIndex = 0;
                 signalIndex < table.signalNames.size(); ++signalIndex) {
                names.append(table.signalNames.at(signalIndex));
                groups.append(group);
            }
        }
        const int firstSignalId = m_signalColors.size();
        colors.resize(names.size());
        for (int i = 0; i < names.size(); ++i)
            colors[i] = QColor::fromHsv(((firstSignalId + i) * 47) % 360,
                                         190, 230);

        QVector<PlotSeriesInput> inputs;
        inputs.reserve(names.size());
        int signalId = firstSignalId;
        for (const LoadedTable &table : tables) {
            for (int signalIndex = 0;
                 signalIndex < table.signalNames.size(); ++signalIndex) {
                PlotSeriesInput input;
                input.id = signalId;
                input.color = colors.at(signalId - firstSignalId);
                input.time = table.time;
                input.values = table.values.value(signalIndex);
                input.monotonicTime = table.monotonicTimes.value(signalIndex,
                                                                  true);
                input.monotonicTimeKnown = true;
                inputs.append(std::move(input));
                ++signalId;
            }
        }
        const bool initializeSharedXRange = m_loadedPaths.isEmpty();
        m_seriesStore->appendSeries(inputs);
        m_signalColors.append(colors);
        m_signals->setPlotCount(m_plotRows * m_plotColumns);
        m_signals->appendNames(names, groups, colors);
        ++m_plotStateRevision;
        emit plotBindingsChanged();
        m_loadedPaths.insert(path);
        m_loadedFileNames.append(fileName);
        m_currentFile = m_loadedFileNames.size() == 1
            ? fileName
            : QStringLiteral("已加载 %1 个文件").arg(m_loadedFileNames.size());
        emit currentFileChanged();
        if (initializeSharedXRange) {
            bool foundTimeBounds = false;
            double timeMinimum = 0.0;
            double timeMaximum = 0.0;
            for (const LoadedTable &table : tables) {
                if (!table.hasTimeBounds) continue;
                if (!foundTimeBounds) {
                    timeMinimum = table.timeMinimum;
                    timeMaximum = table.timeMaximum;
                    foundTimeBounds = true;
                } else {
                    timeMinimum = qMin(timeMinimum, table.timeMinimum);
                    timeMaximum = qMax(timeMaximum, table.timeMaximum);
                }
            }
            if (foundTimeBounds) {
                if (qFuzzyCompare(timeMinimum, timeMaximum)) {
                    m_sharedXMinimum = timeMinimum - 0.5;
                    m_sharedXMaximum = timeMaximum + 0.5;
                } else {
                    const double padding = qMax(
                        (timeMaximum - timeMinimum) * 0.02, 1e-9);
                    m_sharedXMinimum = timeMinimum - padding;
                    m_sharedXMaximum = timeMaximum + padding;
                }
                m_syncingRanges = true;
                for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
                    if (plot) plot->setXRange(m_sharedXMinimum,
                                               m_sharedXMaximum);
                m_syncingRanges = false;
            }
        }
        for (int index = 0; index < m_plots.size(); ++index)
            refreshPlot(index);
        qint64 rows = 0;
        for (const auto &table : tables) rows += table.rowCount;
        m_batchRows += rows;
        m_batchSignals += names.size();
        m_batchSkipped += skipped;
    }
    ++m_batchCompleted;
    if (m_batchTotal > 0)
        setLoadingProgress(qMin(99, m_batchCompleted * 100 / m_batchTotal));
    startNextLoad();
}

void AppController::refreshPlot(int index, bool fitY)
{
    if (index < 0 || index >= m_plots.size() || !m_plots.at(index)) return;
    PlotItem *plot = m_plots.at(index);
    const QVector<int> sortedRows = m_signals->plotRows(index);
    QVector<PlotSeriesId> visibleIds;
    visibleIds.reserve(sortedRows.size());
    for (int row : sortedRows)
        if (row >= 0 && row < m_signalColors.size())
            visibleIds.append(row);
    plot->setVisibleSeries(visibleIds);
    if (fitY) plot->fitY();
}
