// AppController: file import queue, session bookkeeping and Excel/MAT export.
#include "appcontroller.h"
#include "plotitem.h"
#include "dataloadworker.h"
#include "dataexportworker.h"
#include "matwriter.h"

#include <QFileInfo>
#include <QHash>
#include <QJSValue>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <utility>
#include <map>
#include <tuple>

namespace {
// Accepts a QUrl, a file: string or a plain path and returns a local path.
QString localPathFrom(const QVariant &value)
{
    const QUrl url = value.canConvert<QUrl>() ? value.toUrl() : QUrl(value.toString());
    QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
    if (path.startsWith(QStringLiteral("file:"))) path = QUrl(path).toLocalFile();
    return path;
}

bool isSupportedDataFile(const QString &path)
{
    static const QStringList suffixes{QStringLiteral("csv"), QStringLiteral("txt"),
                                      QStringLiteral("xlsx"), QStringLiteral("mat")};
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}
}

bool AppController::loadCsv(const QString &filePath)
{
    return loadFiles(QVariant(filePath)) == 1;
}

int AppController::loadFiles(const QVariant &filePaths)
{
    if (!m_loader || m_exporting || sessionInteractionBlocked()) return 0;
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
        const QFileInfo info(localPathFrom(value));
        QString path = info.canonicalFilePath();
        if (path.isEmpty()) path = info.absoluteFilePath();
        if (path.isEmpty() || !isSupportedDataFile(path)) continue;
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
    if (m_loading || m_exporting || fileName.isEmpty()) return false;
    const QVector<int> removedRows = m_signals->removeFile(fileName);
    if (removedRows.isEmpty()) return false;

    m_seriesStore->removeSeries(QSet<int>(removedRows.cbegin(), removedRows.cend()));
    for (auto it = removedRows.crbegin(); it != removedRows.crend(); ++it)
        if (*it >= 0 && *it < m_signalColors.size())
            m_signalColors.removeAt(*it);
    m_loadedPaths.remove(m_sourcePathsByGroup.take(fileName));
    m_loadedFileNames.removeAll(fileName);
    updateCurrentFileLabel();
    for (int index = 0; index < m_plots.size(); ++index)
        refreshPlot(index, false);
    notifyPlotBindingsChanged();
    setStatus(QStringLiteral("已移除文件：%1").arg(fileName));
    return true;
}

bool AppController::matExportSupported() const
{
    return ::matExportSupported();
}

bool AppController::collectExportTables(int scope, QVector<DataExportTable> *tables)
{
    tables->clear();
    QVector<int> rows;
    if (scope == PlottedSignals) {
        QSet<int> uniqueRows;
        for (int plotIndex = 0; plotIndex < m_plotRows * m_plotColumns;
             ++plotIndex) {
            const QVector<int> plotRows = m_signals->plotRows(plotIndex);
            for (int row : plotRows) uniqueRows.insert(row);
        }
        rows = QVector<int>(uniqueRows.cbegin(), uniqueRows.cend());
        std::sort(rows.begin(), rows.end());
    } else if (scope == AllLoadedData) {
        rows.reserve(signalCount());
        for (int row = 0; row < signalCount(); ++row) rows.append(row);
    } else {
        return false;
    }
    if (rows.isEmpty()) {
        setStatus(QStringLiteral("当前所有子图中没有已绘制的信号"));
        return false;
    }

    const PlotSeriesSnapshot snapshot = m_seriesStore->snapshot(rows);
    QHash<int, PlotSeriesDataPtr> dataById;
    for (const PlotSeriesDataPtr &data : snapshot.series)
        if (data) dataById.insert(data->id, data);

    std::map<std::tuple<QString, int, double>, int> tableBySource;
    for (int row : std::as_const(rows)) {
        const PlotSeriesDataPtr data = dataById.value(row);
        if (!data) continue;
        QString group = m_signals->groupAt(row);
        if (group.isEmpty()) group = QStringLiteral("Data");
        // Display labels are not identities. Keep canonical source and table ordinal.
        const auto exportKey = std::make_tuple(data->sourceFile, data->sourceTable,
                                               data->timeOffset);
        const auto existing = tableBySource.find(exportKey);
        int tableIndex = existing == tableBySource.end() ? -1 : existing->second;
        if (tableIndex < 0) {
            tableIndex = tables->size();
            tableBySource.emplace(exportKey, tableIndex);
            tables->append({group, {}});
        }
        (*tables)[tableIndex].series.append({m_signals->nameAt(row), data});
    }
    return !tables->isEmpty();
}

void AppController::beginExport(const QString &kind)
{
    m_exportKind = kind;
    m_exporter->resetCancellation();
    setExportProgress(0);
    m_exporting = true;
    emit exportingChanged();
    setStatus(QStringLiteral("正在导出 %1…").arg(kind));
}

bool AppController::exportXlsx(const QVariant &filePath, int scope,
                               bool zipCompressionEnabled)
{
    if (!m_exporter || m_exporting || m_loading || signalCount() == 0)
        return false;
    QString path = localPathFrom(filePath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".xlsx"), Qt::CaseInsensitive))
        path += QStringLiteral(".xlsx");

    QVector<DataExportTable> tables;
    if (!collectExportTables(scope, &tables)) return false;

    beginExport(QStringLiteral("Excel"));
    QMetaObject::invokeMethod(
        m_exporter,
        [exporter = m_exporter, path, tables = std::move(tables),
         zipCompressionEnabled]() {
            exporter->exportWorkbook(path, tables, zipCompressionEnabled);
        }, Qt::QueuedConnection);
    return true;
}

bool AppController::exportMat(const QVariant &filePath, int scope)
{
    if (!m_exporter || m_exporting || m_loading || signalCount() == 0)
        return false;
    if (!matExportSupported()) {
        setStatus(QStringLiteral("当前版本未启用 MAT 支持"));
        return false;
    }
    QString path = localPathFrom(filePath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".mat"), Qt::CaseInsensitive))
        path += QStringLiteral(".mat");

    QVector<DataExportTable> tables;
    if (!collectExportTables(scope, &tables)) return false;

    beginExport(QStringLiteral("MAT"));
    QMetaObject::invokeMethod(
        m_exporter,
        [exporter = m_exporter, path, tables = std::move(tables)]() {
            exporter->exportMat(path, tables);
        }, Qt::QueuedConnection);
    return true;
}

void AppController::cancelExport()
{
    if (m_exporting && m_exporter) {
        m_exporter->requestCancel();
        setStatus(QStringLiteral("正在取消导出…"));
    }
}

void AppController::startNextLoad()
{
    if (m_loadQueue.isEmpty()) {
        if (m_pendingSession) { finishSessionRestore(); return; }
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

void AppController::onLoadFinished(const QString &path, const QVector<LoadedTable> &tables,
                                   int skipped, const QString &error)
{
    m_pendingPaths.remove(path);
    if (!error.isEmpty()) {
        ++m_batchErrors;
        if (m_batchFirstError.isEmpty()) m_batchFirstError = error;
    } else {
        if (m_pendingSession) m_stagedSessionTables.append(tables);
        else appendLoadedTables(path, tables);
        qint64 rows = 0;
        for (const auto &table : tables) rows += table.rowCount;
        m_batchRows += rows;
        for (const auto &table : tables) m_batchSignals += table.signalNames.size();
        m_batchSkipped += skipped;
    }
    ++m_batchCompleted;
    if (m_batchTotal > 0)
        setLoadingProgress(qMin(99, m_batchCompleted * 100 / m_batchTotal));
    startNextLoad();
}

void AppController::appendLoadedTables(const QString &path, const QVector<LoadedTable> &tables)
{
    QStringList names;
    QStringList groups;
    const QString baseFileName = QFileInfo(path).fileName();
    QString fileName = baseFileName;
    for (int suffix = 2; m_sourcePathsByGroup.contains(fileName); ++suffix)
        fileName = baseFileName + QStringLiteral(" [%1]").arg(suffix);
    m_sourcePathsByGroup.insert(fileName, path);
    const QString fileBaseName = QFileInfo(path).completeBaseName();
    for (const LoadedTable &table : tables) {
        // A single sheet named after the file collapses into the file node.
        const bool skipTableNode = tables.size() == 1 && table.name == fileBaseName;
        const QString group = skipTableNode ? fileName : fileName + QLatin1Char('/') + table.name;
        for (const QString &signalName : table.signalNames) {
            names.append(signalName);
            groups.append(group);
        }
    }

    const int firstSignalId = m_signalColors.size();
    QVector<QColor> colors(names.size());
    for (QColor &color : colors) {
        color = signalPalette().at(m_nextColorIndex);
        m_nextColorIndex = (m_nextColorIndex + 1) % signalPalette().size();
    }

    QVector<PlotSeriesInput> inputs;
    inputs.reserve(names.size());
    int signalId = firstSignalId;
    int sourceTable = 0;
    for (const LoadedTable &table : tables) {
        for (int signalIndex = 0; signalIndex < table.signalNames.size(); ++signalIndex) {
            PlotSeriesInput input;
            input.id = signalId;
            input.sourceFile = path;
            input.sourceTable = sourceTable;
            input.sourceColumn = signalIndex;
            input.sourceTableName = table.name;
            input.color = colors.at(signalId - firstSignalId);
            input.time = table.time;
            input.values = table.values.value(signalIndex);
            input.monotonicTime = table.monotonicTimes.value(signalIndex, true);
            input.monotonicTimeKnown = true;
            input.rangeIndex = table.rangeIndexes.value(signalIndex);
            inputs.append(std::move(input));
            ++signalId;
        }
        ++sourceTable;
    }

    const bool initializeSharedXRange = m_loadedPaths.isEmpty();
    m_seriesStore->appendSeries(inputs);
    m_signalColors.append(colors);
    m_signals->setPlotCount(m_plotRows * m_plotColumns);
    m_signals->appendNames(names, groups, colors);
    notifyPlotBindingsChanged();
    m_loadedPaths.insert(path);
    m_loadedFileNames.append(fileName);
    updateCurrentFileLabel();

    if (initializeSharedXRange) {
        bool found = false;
        double timeMinimum = 0.0, timeMaximum = 0.0;
        for (const LoadedTable &table : tables) {
            if (!table.hasTimeBounds) continue;
            timeMinimum = found ? qMin(timeMinimum, table.timeMinimum) : table.timeMinimum;
            timeMaximum = found ? qMax(timeMaximum, table.timeMaximum) : table.timeMaximum;
            found = true;
        }
        if (found) {
            if (qFuzzyCompare(timeMinimum, timeMaximum)) {
                m_sharedXMinimum = timeMinimum - 0.5;
                m_sharedXMaximum = timeMaximum + 0.5;
            } else {
                const double padding = qMax((timeMaximum - timeMinimum) * 0.02, 1e-9);
                m_sharedXMinimum = timeMinimum - padding;
                m_sharedXMaximum = timeMaximum + padding;
            }
            applySharedXRange(m_sharedXMinimum, m_sharedXMaximum);
        }
    }
    for (int index = 0; index < m_plots.size(); ++index)
        refreshPlot(index);
}
