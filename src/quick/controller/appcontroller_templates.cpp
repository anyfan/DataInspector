#include "appcontroller.h"
#include "items/plotitem.h"
#include "items/trajectoryitem.h"
#include "render/plotaxisutils.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QScopedValueRollback>
#include <QUuid>
#include <QtMath>
#include <map>
#include <tuple>
#include <algorithm>
#include <cmath>

bool AppController::saveViewTemplate(const QVariant &filePath)
{
    if (m_loading || m_exporting || m_restoringSession || deriving() || (!signalCount() && m_objects.isEmpty())) return false;
    QString path = localSessionPath(filePath);
    if (path.isEmpty()) return false;
    if (!path.endsWith(".diview", Qt::CaseInsensitive)) path += QStringLiteral(".diview");
    QString error;
    if (!writeViewTemplate(path, viewTemplateFromSession(captureSession(path)), &error)) {
        setStatus(QStringLiteral("保存视图模板失败：%1").arg(error)); return false;
    }
    setStatus(QStringLiteral("已保存视图模板：%1").arg(QFileInfo(path).fileName()));
    return true;
}

QVariantMap AppController::previewViewTemplate(const QVariant &filePath) const
{
    ViewTemplateDocument state;
    QString error;
    if (!readViewTemplate(localSessionPath(filePath), &state, &error))
        return {{"error", error}, {"rows", QVariantList{}}};
    const auto current = captureSession({});
    QVariantList options{QVariantMap{{"id", -1}, {"label", QStringLiteral("请选择信号…")}}};
    QHash<QPair<QString, QString>, QVector<int>> byTableName;
    QHash<QPair<int, QString>, QVector<int>> byTableIndex;
    for (int i = 0; i < current.series.size(); ++i) {
        const auto &s = current.series[i];
        if (!s.objectId.isEmpty()) continue;
        byTableName[{s.tableName, s.originalName}].append(i);
        byTableIndex[{s.table, s.originalName}].append(i);
        const QString label = QFileInfo(current.files.value(s.file)).fileName() + " / " + s.tableName + " / " + s.originalName;
        options.append(QVariantMap{{"id", i}, {"label", label}});
    }
    auto ids = templateRawSignals(state).values();
    std::sort(ids.begin(), ids.end());
    QVariantList rows;
    for (int id : ids) {
        const auto &saved = state.series[id];
        const auto exact = byTableName.value({saved.tableName, saved.originalName});
        const auto fallback = byTableIndex.value({saved.table, saved.originalName});
        const auto &candidates = exact.isEmpty() ? fallback : exact;
        const int match = candidates.size() == 1 ? candidates.first() : -1;
        rows.append(QVariantMap{{"id", id}, {"label", saved.sourceName + " / " + saved.tableName + " / " + saved.originalName},
            {"match", match}, {"hint", match >= 0 ? (exact.isEmpty() ? QStringLiteral("按表序号和原始名匹配") : QStringLiteral("按表名和原始名匹配"))
                : (candidates.isEmpty() ? QStringLiteral("缺失，请手动指定") : QStringLiteral("重名，请手动指定"))}});
    }
    int processing = 0, rules = 0;
    for (const auto &object : state.objects) { processing += isProcessingConfiguration(object); rules += object.rules.size(); }
    return {{"error", QString()}, {"rows", rows}, {"options", options}, {"plotCount", state.plots.size()},
        {"objectCount", state.objects.size() - processing}, {"processingCount", processing}, {"ruleCount", rules},
        {"outputCount", state.series.size() - ids.size()}};
}

void AppController::installTemplateWorkspace(const SessionDocument &state, const std::shared_ptr<PlotSeriesStore> &store, const QStringList &groups)
{
    QScopedValueRollback<bool> applying(m_applyingSession, true);
    cancelObjectEvaluation();
    m_seriesStore = store; m_objects = state.objects;
    emit templateWorkspaceChanged();
    QStringList names; QVector<QColor> colors;
    for (const auto &signal : state.series) { names.append(signal.originalName); colors.append(signal.color); }
    m_signalColors = colors; m_signalEnums.clear();
    m_signals->setNames(names, groups, colors);
    for (const auto &plot : std::as_const(m_plots)) if (plot) plot->setSeriesStore(m_seriesStore);
    applyTemplateState(state);
    emit objectsChanged(); emit currentFileChanged();
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
        m_signalEnums.insert(id, s.enums);
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
    if (m_loading || m_exporting || m_restoringSession || deriving()) return false;
    ViewTemplateDocument saved;
    QString error;
    if (!readViewTemplate(localSessionPath(filePath), &saved, &error)) { setStatus(error); return false; }
    const auto required = templateRawSignals(saved);
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
            || it.value().metaType().id() == QMetaType::Bool || isDerivedSignal(int(value)) || targets.contains(int(value))) {
            setStatus(QStringLiteral("信号匹配缺失、重复或无效，当前视图已保留")); return false;
        }
        remap.insert(id, int(value)); targets.insert(int(value));
    }
    const auto before = captureSession({});
    const auto beforeStore = m_seriesStore;
    const auto beforeStates = m_objectEvaluationStates;
    const auto beforeErrors = m_objectErrors;
    QStringList beforeGroups;
    for (int row = 0; row < signalCount(); ++row) beforeGroups.append(m_signals->groupAt(row));
    auto groups = beforeGroups;
    auto state = before;
    state.rows = saved.rows; state.columns = saved.columns; state.active = saved.active; state.solo = saved.solo;
    state.plots = saved.plots;
    // Import all definitions under fresh identities. Raw mappings are the only
    // external assignments; output IDs and all internal references travel together.
    auto imported = saved.objects;
    QHash<QString, QString> identities;
    const auto allocate = [&](const QString &id) { identities.insert(id, QUuid::createUuid().toString(QUuid::WithoutBraces)); };
    for (const auto &object : imported) {
        allocate(object.id);
        for (const auto &field : object.fields) allocate(field.id);
        for (const auto &rule : object.rules) { allocate(rule.id); for (const auto &output : rule.outputs) allocate(output.id); }
    }
    QVector<PlotSeriesInput> placeholders;
    for (auto &object : imported) {
        object.id = identities.value(object.id);
        for (auto &field : object.fields) { field.id = identities.value(field.id); if (field.series >= 0) field.series = remap.value(field.series, -1); }
        for (int r = 0; r < object.rules.size(); ++r) {
            auto &rule = object.rules[r]; rule.id = identities.value(rule.id);
            for (auto &input : rule.inputs) input = identities.value(input);
            for (int k = 0; k < rule.outputs.size(); ++k) {
                auto &output = rule.outputs[k];
                const int savedRow = output.series;
                const auto &style = saved.series[savedRow];
                output.id = identities.value(output.id);
                for (auto &input : output.inputs) input = identities.value(input);
                output.series = state.series.size(); remap.insert(savedRow, output.series);
                SessionSignal signal;
                signal.objectId = object.id; signal.outputId = output.id; signal.table = r; signal.column = k;
                signal.tableName = object.name + '/' + rule.name; signal.originalName = style.originalName;
                signal.name = style.name; signal.color = style.color; signal.width = style.width; signal.style = style.style; signal.enums = style.enums;
                state.series.append(signal); groups.append(objectOutputGroup(object));
                PlotSeriesInput input; input.id = output.series; input.sourceFile = "object:" + object.id;
                input.sourceTable = r; input.sourceColumn = k; input.sourceTableName = signal.tableName;
                input.color = signal.color; input.lineWidth = signal.width; input.lineStyle = Qt::PenStyle(signal.style);
                input.step = rule.operation == "bits" || rule.operation == "bitfield" || rule.operation == "packetloss";
                placeholders.append(input);
            }
        }
        state.objects.append(object);
    }
    if (state.series.size() > 100000 || !validateObjects(state.objects, state.series.size(), &error)) {
        setStatus(QStringLiteral("无法导入模板配置：%1").arg(error)); return false;
    }
    for (auto it = saved.series.cbegin(); it != saved.series.cend(); ++it) {
        const int id = it.key();
        const auto &style = saved.series[id];
        auto &target = state.series[remap[id]];
        target.enums = style.enums; target.name = style.name; target.color = style.color; target.width = style.width; target.style = style.style;
    }
    QSet<int> visible;
    for (auto &plot : state.plots) {
        for (auto &lane : plot.states.lanes) { lane.source = remap[lane.source]; if (plot.states.enabled && lane.visible) visible.insert(lane.source); }
        for (int &id : plot.seriesIds) { id = remap[id]; if (!plot.trajectory.enabled) visible.insert(id); }
        auto &trajectory = plot.trajectory;
        for (int &id : trajectory.signalIds) id = remap[id];
        auto tracks = trajectory.entries();
        for (auto &track : tracks) {
            if (!track.objectId.isEmpty()) track.objectId = identities.value(track.objectId);
            for (int &id : track.axes) if (id >= 0) { id = remap[id]; if (trajectory.enabled && track.visible) visible.insert(id); }
            for (int &id : track.attitude.sources) if (id >= 0) {
                id = remap[id];
                if (trajectory.enabled && track.visible && track.attitude.mode) visible.insert(id);
            }
        }
        trajectory.tracks = tracks;

    }
    QVector<int> visibleIds(visible.cbegin(), visible.cend());
    auto store = std::make_shared<PlotSeriesStore>(*m_seriesStore);
    store->appendSeries(placeholders);
    const auto snapshot = store->snapshot(visibleIds);
    if (fixedRanges) {
        state.xMinimum = saved.xMinimum; state.xMaximum = saved.xMaximum;
    } else if (const auto bounds = PlotSeriesStore::timeBounds(snapshot)) {
        if (const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5)) {
            state.xMinimum = range->first; state.xMaximum = range->second;
        }
    }
    if (!fixedRanges) for (auto &plot : state.plots) {
        const auto bounds = PlotSeriesStore::bounds(store->snapshot(plot.seriesIds), state.xMinimum, state.xMaximum);
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
    installTemplateWorkspace(state, store, groups);
    markSessionModified();
    m_templateUndo = before; m_templateUndoData = beforeStore; m_templateUndoGroups = beforeGroups;
    m_templateUndoEvaluationStates = beforeStates; m_templateUndoErrors = beforeErrors;
    m_templateAppliedObjects = objectsToJson(m_objects);
    scheduleObjectEvaluation();
    m_templateUndoStore = m_seriesStore; m_templateUndoGeneration = m_seriesStore->generation();
    m_templateFitPending = !fixedRanges && !placeholders.isEmpty() && deriving();
    { QScopedValueRollback<bool> applying(m_applyingSession, true); emit plotBindingsChanged(); }
    setStatus(QStringLiteral("已应用视图模板，可从会话菜单撤销"));
    return true;
}

bool AppController::undoViewTemplate()
{
    if (m_loading || m_exporting || m_restoringSession || !canUndoViewTemplate()) return false;
    const auto state = *m_templateUndo;
    const auto store = m_templateUndoData;
    const auto groups = m_templateUndoGroups;
    m_templateUndo.reset();
    installTemplateWorkspace(state, store, groups);
    m_objectEvaluationStates = m_templateUndoEvaluationStates; m_objectErrors = m_templateUndoErrors;
    clearTemplateUndo();
    emit objectsChanged();
    markSessionModified();
    setStatus(QStringLiteral("已撤销视图模板应用"));
    return true;
}

void AppController::clearTemplateUndo()
{
    m_templateUndo.reset(); m_templateUndoData.reset(); m_templateUndoGroups.clear();
    m_templateUndoEvaluationStates.clear(); m_templateUndoErrors.clear(); m_templateFitPending = false;
    m_templateAppliedObjects = {};
}

void AppController::finishTemplateEvaluation()
{
    if (!m_templateFitPending) return;
    m_templateFitPending = false;
    auto state = captureSession({});
    QSet<int> visible;
    for (const auto &plot : state.plots) {
        if (plot.trajectory.enabled) {
            for (const auto &track : plot.trajectory.entries()) if (track.visible) {
                for (int id : track.axes) if (id >= 0) visible.insert(id);
                if (track.attitude.mode) for (int id : track.attitude.sources) if (id >= 0) visible.insert(id);
            }
        } else if (plot.states.enabled) {
            for (const auto &lane : plot.states.lanes) if (lane.visible) visible.insert(lane.source);
        } else for (int id : plot.seriesIds) visible.insert(id);
    }
    const QVector<int> ids(visible.cbegin(), visible.cend());
    const auto snapshot = m_seriesStore->snapshot(ids);
    if (const auto bounds = PlotSeriesStore::timeBounds(snapshot)) {
        if (const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5)) {
            state.xMinimum = range->first; state.xMaximum = range->second;
        }
    }
    for (auto &plot : state.plots) {
        const auto bounds = PlotSeriesStore::bounds(m_seriesStore->snapshot(plot.seriesIds), state.xMinimum, state.xMaximum);
        if (plot.normalizeY) { plot.yMinimum = -.05; plot.yMaximum = 1.05; }
        else if (bounds) {
            const auto range = paddedPlotRange(bounds->yMinimum, bounds->yMaximum, .05,
                bounds->yMinimum == 0 ? .5 : qMax(std::abs(bounds->yMinimum) * .05, std::numeric_limits<double>::denorm_min()));
            if (range) { plot.yMinimum = range->first; plot.yMaximum = range->second; }
        } else { plot.yMinimum = 0; plot.yMaximum = 1; }
    }
    const double span = state.xMaximum - state.xMinimum;
    state.cursor.x1 = PlotSeriesStore::nearestX(snapshot, state.xMinimum + span * .25).value_or(state.xMinimum + span * .25);
    state.cursor.x2 = PlotSeriesStore::nearestX(snapshot, state.xMinimum + span * .75).value_or(state.xMinimum + span * .75);
    applyTemplateState(state);
}
