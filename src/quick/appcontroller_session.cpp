#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "render/plotaxisutils.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QScopedValueRollback>
#include <QtMath>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

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
    markSessionModified();
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

SessionDocument AppController::captureSession(const QString &path) const
{
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
        view.trajectory = m_trajectories.value(index);
        state.plots.append(view);
    }
    return state;
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
    const auto state = captureSession(path);
    QString error;
    if (!writeSessionDocument(path, state, &error)) {
        setStatus(QStringLiteral("保存会话失败：%1").arg(error)); emit sessionError(status()); return false;
    }
    m_sessionPath = path;
    m_sessionModified = false;
    emit sessionModifiedChanged();
    emit sessionPathChanged();
    setStatus(QStringLiteral("已保存会话：%1").arg(QFileInfo(path).fileName()));
    return true;
}

bool AppController::restoreSession(const QVariant &filePath)
{
    return restoreSessionWithFiles(filePath, {});
}

QVariantList AppController::missingSessionFiles(const QVariant &filePath) const
{
    const QString path = localSessionPath(filePath);
    SessionDocument state;
    QString error;
    QVariantList missing;
    if (!readSessionDocument(path, &state, &error)) return missing;
    const QDir directory(QFileInfo(path).absolutePath());
    for (int i = 0; i < state.files.size(); ++i) {
        const QFileInfo info(directory.absoluteFilePath(state.files[i]));
        if (!info.isFile() || !info.isReadable())
            missing.append(QVariantMap{{"index", i}, {"path", info.absoluteFilePath()}});
    }
    return missing;
}

bool AppController::restoreSessionWithFiles(const QVariant &filePath, const QVariantMap &replacements)
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
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) {
        bool valid = false;
        const int index = it.key().toInt(&valid);
        if (!valid || QString::number(index) != it.key() || index < 0 || index >= state.files.size())
            return fail(QStringLiteral("重新定位的文件索引无效：%1").arg(it.key()));
    }
    QStringList sources;
    QSet<QString> uniquePaths;
    const QDir directory(QFileInfo(path).absolutePath());
    const QStringList supported{QStringLiteral("csv"), QStringLiteral("txt"), QStringLiteral("xlsx"), QStringLiteral("mat")};
    for (int i = 0; i < state.files.size(); ++i) {
        const QString reference = replacements.contains(QString::number(i))
            ? localSessionPath(replacements.value(QString::number(i))) : directory.absoluteFilePath(state.files[i]);
        const QFileInfo info(reference);
        if (!info.isFile() || !info.isReadable()) return fail(QStringLiteral("数据文件缺失或不可读：%1").arg(info.absoluteFilePath()));
        const QString source = info.canonicalFilePath();
        if (!supported.contains(info.suffix().toLower()) || uniquePaths.contains(source))
            return fail(QStringLiteral("数据文件类型不受支持或来源重复：%1").arg(source));
        uniquePaths.insert(source); sources.append(source);
    }
    // Stage all sources without changing the current model, plots or series store.
    m_pendingSession = std::move(state);
    m_sessionRelocated = !replacements.isEmpty();
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
    if (success) {
        m_sessionModified = m_sessionRelocated;
        emit sessionModifiedChanged();
    }
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
    m_plotViews.clear(); m_trajectories.clear();
    for (int i = 0; i < state.plots.size(); ++i) {
        m_plotViews.insert(i, state.plots.at(i));
        auto trajectory = state.plots.at(i).trajectory;
        auto tracks = trajectory.entries();
        for (auto &track : tracks) {
            for (int &id : track.axes) if (id >= 0) id = savedToCurrent.at(id);
            for (int &id : track.attitude.sources) if (id >= 0) id = savedToCurrent.at(id);
        }
        trajectory.tracks = tracks; static_cast<SessionTrajectoryEntry &>(trajectory) = tracks[trajectory.active];
        for (int &id : trajectory.signalIds) id = savedToCurrent.at(id);
        m_trajectories.insert(i, trajectory);
    }
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
    for (int i = 0; i < m_trajectoryPlots.size(); ++i) if (m_trajectoryPlots[i]) {
        m_trajectoryPlots[i]->setCamera(m_trajectories.value(i).camera); refreshTrajectory(i);
    }
    syncTrajectoryCursors();
    updateCurrentFileLabel(); notifyPlotBindingsChanged();
    m_sessionPath = m_pendingSessionPath; emit sessionPathChanged();
    completeSessionRestore(true, QStringLiteral("已恢复会话：%1（%2 个文件，%3 个信号）")
                           .arg(QFileInfo(m_sessionPath).fileName()).arg(m_loadedFileNames.size()).arg(signalCount()));
}

namespace {
QSet<int> templateSignalIds(const SessionDocument &state)
{
    QSet<int> ids;
    for (const auto &plot : state.plots) {
        for (int id : plot.seriesIds) ids.insert(id);
        for (int id : plot.trajectory.signalIds) ids.insert(id);
        for (const auto &track : plot.trajectory.entries()) {
            for (int id : track.axes) if (id >= 0) ids.insert(id);
            for (int id : track.attitude.sources) if (id >= 0) ids.insert(id);
        }
    }
    return ids;
}
}

bool AppController::saveViewTemplate(const QVariant &filePath)
{
    if (m_loading || m_exporting || m_restoringSession || !signalCount()) return false;
    QString path = localSessionPath(filePath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".diview", Qt::CaseInsensitive)) path += QStringLiteral(".diview");
    QString error;
    if (!writeViewTemplate(path, captureSession(path), &error)) {
        setStatus(QStringLiteral("保存视图模板失败：%1").arg(error)); return false;
    }
    setStatus(QStringLiteral("已保存视图模板：%1").arg(QFileInfo(path).fileName()));
    return true;
}

QVariantMap AppController::previewViewTemplate(const QVariant &filePath) const
{
    SessionDocument state;
    QString error;
    if (!readViewTemplate(localSessionPath(filePath), &state, &error))
        return {{"error", error}, {"rows", QVariantList{}}};
    const auto current = captureSession({});
    QVariantList options{QVariantMap{{"id", -1}, {"label", QStringLiteral("请选择信号…")}}};
    QHash<QPair<QString, QString>, QVector<int>> byTableName;
    QHash<QPair<int, QString>, QVector<int>> byTableIndex;
    for (int i = 0; i < current.series.size(); ++i) {
        const auto &s = current.series[i];
        byTableName[{s.tableName, s.originalName}].append(i);
        byTableIndex[{s.table, s.originalName}].append(i);
        const QString label = QFileInfo(current.files.value(s.file)).fileName() + " / " + s.tableName + " / " + s.originalName;
        options.append(QVariantMap{{"id", i}, {"label", label}});
    }
    auto ids = templateSignalIds(state).values();
    std::sort(ids.begin(), ids.end());
    QVariantList rows;
    for (int id : ids) {
        const auto &saved = state.series[id];
        const auto exact = byTableName.value({saved.tableName, saved.originalName});
        const auto fallback = byTableIndex.value({saved.table, saved.originalName});
        const auto &candidates = exact.isEmpty() ? fallback : exact;
        const int match = candidates.size() == 1 ? candidates.first() : -1;
        rows.append(QVariantMap{{"id", id}, {"label", QFileInfo(state.files.value(saved.file)).fileName() + " / " + saved.tableName + " / " + saved.originalName},
            {"match", match}, {"hint", match >= 0 ? (exact.isEmpty() ? QStringLiteral("按表序号和原始名匹配") : QStringLiteral("按表名和原始名匹配"))
                : (candidates.isEmpty() ? QStringLiteral("缺失，请手动指定") : QStringLiteral("重名，请手动指定"))}});
    }
    return {{"error", QString()}, {"rows", rows}, {"options", options}, {"plotCount", state.plots.size()}};
}

void AppController::applyTemplateState(const SessionDocument &state)
{
    QScopedValueRollback<bool> applying(m_applyingSession, true);
    QScopedValueRollback<bool> cursors(m_syncingCursors, true);
    QScopedValueRollback<bool> ranges(m_syncingRanges, true);
    clearViewHistory();
    m_initialSignalFitDone = true;
    // Clear all current bindings before changing the layout; newly created delegates
    // receive the completed cached state through attachPlot().
    for (int i = 0; i < m_plotRows * m_plotColumns; ++i)
        for (int id : m_signals->plotRows(i)) m_signals->setPlotChecked(i, id, false);
    m_plotViews.clear(); m_trajectories.clear();
    for (int i = 0; i < state.plots.size(); ++i) {
        m_plotViews.insert(i, state.plots[i]);
        m_trajectories.insert(i, state.plots[i].trajectory);
    }
    m_sessionCursor = state.cursor; m_haveCursorState = true;
    m_sharedXMinimum = state.xMinimum; m_sharedXMaximum = state.xMaximum;
    setLayout(state.rows, state.columns);
    for (int i = 0; i < state.plots.size(); ++i)
        for (int id : m_signals->plotRows(i)) m_signals->setPlotChecked(i, id, false);
    for (int id = 0; id < state.series.size(); ++id) {
        const auto &s = state.series[id];
        m_seriesStore->updateSeriesPen(id, s.color, s.width, Qt::PenStyle(s.style));
        m_signalColors[id] = s.color;
        m_signals->renameSignal(id, s.name);
        m_signals->setSignalPen(id, s.color, s.width, Qt::PenStyle(s.style));
    }
    for (int i = 0; i < state.plots.size(); ++i)
        for (int id : state.plots[i].seriesIds) m_signals->setPlotChecked(i, id, true);
    setActivePlot(state.active); setSoloPlot(state.solo);
    emit sessionRestored(state.cursor.mode);
    for (int i = 0; i < m_plots.size(); ++i) if (auto *plot = plotAt(i)) {
        refreshPlot(i, false);
        if (i < state.plots.size()) applyPlotView(plot, state.plots[i]);
    }
    for (int i = 0; i < m_trajectoryPlots.size(); ++i) if (m_trajectoryPlots[i]) {
        m_trajectoryPlots[i]->setCamera(m_trajectories.value(i).camera); refreshTrajectory(i);
    }
    syncTrajectoryCursors(); notifyPlotBindingsChanged();
}

bool AppController::applyViewTemplate(const QVariant &filePath, const QVariantMap &mapping, bool fixedRanges)
{
    if (m_loading || m_exporting || m_restoringSession || !signalCount()) return false;
    SessionDocument saved;
    QString error;
    if (!readViewTemplate(localSessionPath(filePath), &saved, &error)) { setStatus(error); return false; }
    const auto required = templateSignalIds(saved);
    QHash<int, int> remap;
    QSet<int> targets;
    // No mutation until every explicit assignment has been validated.
    if (mapping.size() != required.size()) { setStatus(QStringLiteral("请完成全部信号匹配")); return false; }
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
        bool keyOk = false, valueOk = false;
        const int id = it.key().toInt(&keyOk);
        const double value = it.value().toDouble(&valueOk);
        if (!keyOk || it.key() != QString::number(id) || !required.contains(id)
            || !valueOk || !qIsFinite(value) || value < 0 || value >= signalCount() || std::floor(value) != value
            || targets.contains(int(value))) {
            setStatus(QStringLiteral("信号匹配缺失、重复或无效，当前视图已保留")); return false;
        }
        remap.insert(id, int(value)); targets.insert(int(value));
    }
    const auto before = captureSession({});
    auto state = before;
    state.rows = saved.rows; state.columns = saved.columns; state.active = saved.active; state.solo = saved.solo;
    state.plots = saved.plots;
    for (int id : required) {
        const auto &style = saved.series[id];
        auto &target = state.series[remap[id]];
        target.name = style.name; target.color = style.color; target.width = style.width; target.style = style.style;
    }
    QSet<int> visible;
    for (auto &plot : state.plots) {
        for (int &id : plot.seriesIds) { id = remap[id]; if (!plot.trajectory.enabled) visible.insert(id); }
        auto &trajectory = plot.trajectory;
        for (int &id : trajectory.signalIds) id = remap[id];
        auto tracks = trajectory.entries();
        for (auto &track : tracks) {
            for (int &id : track.axes) if (id >= 0) { id = remap[id]; if (trajectory.enabled && track.visible) visible.insert(id); }
            for (int &id : track.attitude.sources) if (id >= 0) {
                id = remap[id];
                if (trajectory.enabled && track.visible && track.attitude.mode) visible.insert(id);
            }
        }
        trajectory.tracks = tracks;
        static_cast<SessionTrajectoryEntry &>(trajectory) = tracks[trajectory.active];
    }
    QVector<int> visibleIds(visible.cbegin(), visible.cend());
    const auto snapshot = m_seriesStore->snapshot(visibleIds);
    if (fixedRanges) {
        state.xMinimum = saved.xMinimum; state.xMaximum = saved.xMaximum;
    } else if (const auto bounds = PlotSeriesStore::timeBounds(snapshot)) {
        if (const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5)) {
            state.xMinimum = range->first; state.xMaximum = range->second;
        }
    }
    if (!fixedRanges) for (auto &plot : state.plots) {
        const auto bounds = PlotSeriesStore::bounds(m_seriesStore->snapshot(plot.seriesIds), state.xMinimum, state.xMaximum);
        if (plot.normalizeY) { plot.yMinimum = -0.05; plot.yMaximum = 1.05; }
        else if (bounds) {
            const auto range = paddedPlotRange(bounds->yMinimum, bounds->yMaximum, .05,
                bounds->yMinimum == 0 ? .5 : qMax(std::abs(bounds->yMinimum) * .05, std::numeric_limits<double>::denorm_min()));
            if (range) { plot.yMinimum = range->first; plot.yMaximum = range->second; }
        } else { plot.yMinimum = 0; plot.yMaximum = 1; }
    }
    // Keep the current mode, but initialize positions on the new data instead
    // of carrying absolute timestamps from the template.
    const double span = state.xMaximum - state.xMinimum;
    const auto x1 = PlotSeriesStore::nearestX(snapshot, state.xMinimum + span * .25);
    const auto x2 = PlotSeriesStore::nearestX(snapshot, state.xMinimum + span * .75);
    state.cursor.x1 = x1.value_or(state.xMinimum + span * .25);
    state.cursor.x2 = x2.value_or(state.xMinimum + span * .75);
    SessionDocument validated;
    if (!sessionFromJson(sessionToJson(state), &validated, &error)) { setStatus(error); return false; }
    applyTemplateState(state);
    markSessionModified();
    m_templateUndo = before; m_templateUndoStore = m_seriesStore; m_templateUndoGeneration = m_seriesStore->generation();
    emit plotBindingsChanged();
    setStatus(QStringLiteral("已应用视图模板，可从会话菜单撤销"));
    return true;
}

bool AppController::undoViewTemplate()
{
    if (m_loading || m_exporting || m_restoringSession || !canUndoViewTemplate()) return false;
    const auto state = *m_templateUndo;
    m_templateUndo.reset();
    applyTemplateState(state);
    markSessionModified();
    setStatus(QStringLiteral("已撤销视图模板应用"));
    return true;
}
