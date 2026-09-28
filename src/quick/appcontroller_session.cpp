#include "appcontroller.h"
#include "plotitem.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QScopedValueRollback>
#include <QtMath>
#include <map>
#include <tuple>

namespace {
QString localSessionPath(const QVariant &value)
{
    const QUrl url = value.toUrl();
    const QString path = url.isLocalFile() ? url.toLocalFile() : value.toString();
    return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
}
}

SessionPlot AppController::capturePlotView(PlotItem *plot) const
{
    SessionPlot state;
    if (plot) {
        state.yMinimum = plot->yMinimum(); state.yMaximum = plot->yMaximum();
        state.normalizeY = plot->normalizeY(); state.lineWidth = plot->lineWidth();
    }
    return state;
}

void AppController::cachePlotView(PlotItem *plot)
{
    if (m_applyingSession) return; // Restored views remain authoritative until commit ends.
    const int index = m_plots.indexOf(plot);
    if (index >= 0) m_plotViews.insert(index, capturePlotView(plot));
}

void AppController::applyPlotView(PlotItem *plot, const SessionPlot &view)
{
    QScopedValueRollback<bool> ranges(m_syncingRanges, true);
    QScopedValueRollback<bool> cursors(m_syncingCursors, true);
    plot->setNormalizeY(view.normalizeY);
    plot->setLineWidth(view.lineWidth);
    plot->setXRange(m_sharedXMinimum, m_sharedXMaximum);
    plot->setYRange(view.yMinimum, view.yMaximum);
    if (m_haveCursorState)
        plot->restoreCursorState(m_sessionCursor.mode, m_sessionCursor.x1, m_sessionCursor.x2);
}

bool AppController::saveSession(const QVariant &filePath)
{
    if (m_loading || m_exporting || m_restoringSession) {
        setStatus(QStringLiteral("加载、导出或恢复期间不能保存会话")); return false;
    }
    QString path = localSessionPath(filePath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".disession", Qt::CaseInsensitive) && !path.endsWith(".json", Qt::CaseInsensitive))
        path += QStringLiteral(".disession");
    const QDir directory(QFileInfo(path).absolutePath());
    SessionDocument state;
    QHash<QString, int> fileIndices;
    for (const auto &label : m_loadedFileNames) {
        const QString source = m_sourcePathsByGroup.value(label);
        fileIndices.insert(source, state.files.size());
        state.files.append(directory.relativeFilePath(source));
    }
    QVector<int> ids;
    for (int id = 0; id < signalCount(); ++id) ids.append(id);
    const auto snapshot = m_seriesStore->snapshot(ids);
    for (const auto &data : snapshot.series) {
        SessionSignal signal;
        signal.file = fileIndices.value(data->sourceFile, -1);
        signal.table = data->sourceTable; signal.column = data->sourceColumn;
        signal.tableName = data->sourceTableName;
        signal.originalName = m_signals->originalNameAt(data->id);
        signal.name = m_signals->nameAt(data->id);
        signal.color = data->color; signal.width = data->lineWidth;
        signal.style = int(data->lineStyle); signal.timeOffset = data->timeOffset;
        state.series.append(signal);
    }
    state.rows = m_plotRows; state.columns = m_plotColumns;
    state.active = activePlotIndex(); state.solo = m_soloPlotIndex;
    state.xMinimum = m_sharedXMinimum; state.xMaximum = m_sharedXMaximum;
    state.cursor = m_sessionCursor;
    PlotItem *cursorSource = plotAt(state.active);
    if (!cursorSource) for (const auto &plot : std::as_const(m_plots)) if (plot) { cursorSource = plot; break; }
    if (cursorSource)
        state.cursor = {cursorSource->cursorMode(), cursorSource->cursorX1(), cursorSource->cursorX2()};
    state.plots.clear();
    for (int index = 0; index < state.rows * state.columns; ++index) {
        auto view = plotAt(index) ? capturePlotView(plotAt(index)) : m_plotViews.value(index);
        view.seriesIds = m_signals->plotRows(index);
        state.plots.append(view);
    }
    QString error;
    if (!writeSessionDocument(path, state, &error)) {
        setStatus(QStringLiteral("保存会话失败：%1").arg(error)); emit sessionError(status()); return false;
    }
    m_sessionPath = path;
    emit sessionPathChanged();
    setStatus(QStringLiteral("已保存会话：%1").arg(QFileInfo(path).fileName()));
    return true;
}

bool AppController::restoreSession(const QVariant &filePath)
{
    if (m_loading || m_exporting || m_restoringSession) {
        setStatus(QStringLiteral("加载、导出或恢复期间不能打开另一会话")); return false;
    }
    const QString path = localSessionPath(filePath);
    SessionDocument state;
    QString error;
    auto fail = [&](const QString &message) {
        setStatus(QStringLiteral("恢复会话失败：%1").arg(message));
        emit sessionError(status()); emit sessionRestoreFinished(false, status()); return false;
    };
    if (path.isEmpty() || !readSessionDocument(path, &state, &error)) return fail(error);
    QStringList sources;
    QSet<QString> uniquePaths;
    const QDir directory(QFileInfo(path).absolutePath());
    const QStringList supported{QStringLiteral("csv"), QStringLiteral("txt"), QStringLiteral("xlsx"), QStringLiteral("mat")};
    for (const auto &reference : state.files) {
        const QFileInfo info(directory.absoluteFilePath(reference));
        if (!info.isFile() || !info.isReadable()) return fail(QStringLiteral("数据文件缺失或不可读：%1").arg(info.absoluteFilePath()));
        const QString source = info.canonicalFilePath();
        if (!supported.contains(info.suffix().toLower()) || uniquePaths.contains(source))
            return fail(QStringLiteral("数据文件类型不受支持或来源重复：%1").arg(source));
        uniquePaths.insert(source); sources.append(source);
    }
    // Stage all sources without changing the current model, plots or series store.
    m_pendingSession = std::move(state);
    m_pendingSessionPath = path;
    m_sessionSourcePaths = sources;
    m_stagedSessionTables.clear();
    m_batchTotal = sources.size(); m_batchCompleted = 0; m_batchErrors = 0;
    m_batchSignals = 0; m_batchRows = 0; m_batchSkipped = 0; m_batchFirstError.clear();
    m_loadQueue.clear(); m_pendingPaths.clear(); m_activeLoadPath.clear();
    for (const auto &source : sources) { m_loadQueue.enqueue(source); m_pendingPaths.insert(source); }
    setLoadingProgress(0);
    m_restoringSession = true; m_loading = true;
    emit restoringSessionChanged(); emit loadingChanged();
    startNextLoad();
    return true;
}

void AppController::completeSessionRestore(bool success, const QString &message)
{
    m_pendingSession.reset(); m_stagedSessionTables.clear(); m_sessionSourcePaths.clear();
    m_pendingSessionPath.clear(); m_loadQueue.clear(); m_pendingPaths.clear(); m_activeLoadPath.clear();
    m_applyingSession = false; m_restoringSession = false; m_loading = false;
    if (success) setLoadingProgress(100);
    emit loadingChanged(); emit restoringSessionChanged();
    setStatus(message);
    if (!success) emit sessionError(message);
    emit sessionRestoreFinished(success, message);
}

void AppController::finishSessionRestore()
{
    if (m_batchErrors || m_stagedSessionTables.size() != m_sessionSourcePaths.size()) {
        completeSessionRestore(false, QStringLiteral("恢复会话失败，当前会话已保留：%1").arg(m_batchFirstError)); return;
    }
    const SessionDocument state = *m_pendingSession;
    std::map<std::tuple<int, int, int>, int> savedByIdentity;
    for (int i = 0; i < state.series.size(); ++i) {
        const auto &s = state.series.at(i);
        savedByIdentity.emplace(std::make_tuple(s.file, s.table, s.column), i);
    }
    QVector<PlotSeriesInput> inputs;
    QVector<int> savedToCurrent(state.series.size(), -1);
    QStringList originalNames, groups, labels;
    QHash<QString, QString> pathsByGroup;
    QVector<QColor> colors;
    auto schemaFailure = [&](const QString &source) {
        completeSessionRestore(false, QStringLiteral("恢复会话失败，数据表/信号结构或偏移已不匹配，当前会话已保留：%1").arg(source));
    };
    for (int file = 0; file < m_sessionSourcePaths.size(); ++file) {
        const QString source = m_sessionSourcePaths.at(file);
        const QFileInfo info(source);
        QString label = info.fileName();
        for (int suffix = 2; pathsByGroup.contains(label); ++suffix)
            label = info.fileName() + QStringLiteral(" [%1]").arg(suffix);
        labels.append(label); pathsByGroup.insert(label, source);
        const auto &tables = m_stagedSessionTables.at(file);
        for (int tableIndex = 0; tableIndex < tables.size(); ++tableIndex) {
            const auto &table = tables.at(tableIndex);
            const QString group = tables.size() == 1 && table.name == info.completeBaseName()
                ? label : label + QLatin1Char('/') + table.name;
            for (int column = 0; column < table.signalNames.size(); ++column) {
                const auto match = savedByIdentity.find(std::make_tuple(file, tableIndex, column));
                if (match == savedByIdentity.end()) { schemaFailure(source); return; }
                const auto &s = state.series.at(match->second);
                if (s.originalName != table.signalNames.at(column) || s.tableName != table.name
                    || (table.hasTimeBounds && (!qIsFinite(table.timeMinimum + s.timeOffset)
                                                || !qIsFinite(table.timeMaximum + s.timeOffset)))) {
                    schemaFailure(source); return;
                }
                PlotSeriesInput input;
                input.id = inputs.size(); input.sourceFile = source;
                input.sourceTable = tableIndex; input.sourceColumn = column; input.sourceTableName = table.name;
                input.time = table.time; input.values = table.values.value(column);
                input.monotonicTime = table.monotonicTimes.value(column, false); input.monotonicTimeKnown = true;
                input.rangeIndex = table.rangeIndexes.value(column); input.timeOffset = s.timeOffset;
                input.color = s.color; input.lineWidth = s.width; input.lineStyle = Qt::PenStyle(s.style);
                savedToCurrent[match->second] = input.id;
                inputs.append(input); colors.append(s.color); originalNames.append(s.originalName); groups.append(group);
            }
        }
    }
    if (inputs.size() != state.series.size() || savedToCurrent.contains(-1)) { schemaFailure(m_pendingSessionPath); return; }
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries(inputs);

    // Commit only after every source and stable signal identity has been validated.
    clearViewHistory();
    m_applyingSession = true;
    m_initialSignalFitDone = true; // Preserve the restored shared time range.
    QScopedValueRollback<bool> cursors(m_syncingCursors, true);
    QScopedValueRollback<bool> ranges(m_syncingRanges, true);
    m_seriesStore = std::move(store);
    m_signalColors = colors; m_nextColorIndex = colors.size() % signalPalette().size();
    m_loadedPaths = QSet<QString>(m_sessionSourcePaths.cbegin(), m_sessionSourcePaths.cend());
    m_loadedFileNames = labels; m_sourcePathsByGroup = pathsByGroup;
    m_signals->setFilter({}); m_signals->setNames(originalNames, groups, colors);
    m_plotViews.clear();
    for (int i = 0; i < state.plots.size(); ++i) m_plotViews.insert(i, state.plots.at(i));
    m_sessionCursor = state.cursor; m_haveCursorState = true;
    m_sharedXMinimum = state.xMinimum; m_sharedXMaximum = state.xMaximum;
    setLayout(state.rows, state.columns);
    for (int i = 0; i < state.series.size(); ++i) {
        const auto &s = state.series.at(i); const int id = savedToCurrent.at(i);
        m_signals->renameSignal(id, s.name);
        m_signals->setSignalPen(id, s.color, s.width, Qt::PenStyle(s.style));
    }
    for (int plot = 0; plot < state.plots.size(); ++plot)
        for (int id : state.plots.at(plot).seriesIds)
            m_signals->setPlotChecked(plot, savedToCurrent.at(id), true);
    setActivePlot(state.active); setSoloPlot(state.solo);
    // QML toolbar bindings are synchronized before setting exact cursor coordinates.
    emit sessionRestored(state.cursor.mode);
    for (int i = 0; i < m_plots.size(); ++i) if (auto *plot = plotAt(i)) {
        plot->setSeriesStore(m_seriesStore); refreshPlot(i, false);
        applyPlotView(plot, state.plots.at(i));
    }
    updateCurrentFileLabel(); notifyPlotBindingsChanged();
    m_sessionPath = m_pendingSessionPath; emit sessionPathChanged();
    completeSessionRestore(true, QStringLiteral("已恢复会话：%1（%2 个文件，%3 个信号）")
                           .arg(QFileInfo(m_sessionPath).fileName()).arg(m_loadedFileNames.size()).arg(signalCount()));
}
