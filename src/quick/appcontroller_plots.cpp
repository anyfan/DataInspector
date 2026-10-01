// AppController: subplot bindings, legend actions, layout and view fitting.
#include "appcontroller.h"
#include "plotitem.h"
#include "trajectoryitem.h"
#include "render/plotaxisutils.h"

#include <utility>
#include <algorithm>
#include <QtMath>
#include <QScopedValueRollback>

void AppController::beginViewChange()
{
    if (m_viewChangeDepth++ == 0) m_viewChangeRecorded = false;
}

void AppController::endViewChange()
{
    if (m_viewChangeDepth > 0) --m_viewChangeDepth;
    if (m_viewChangeDepth == 0) m_viewChangeRecorded = false;
}

void AppController::recordViewChange()
{
    if (m_restoringView || m_syncingRanges || m_loading || m_restoringSession
        || m_applyingSession || (m_viewChangeDepth > 0 && m_viewChangeRecorded)) return;
    ViewState state{m_sharedXMinimum, m_sharedXMaximum, {}, {}};
    for (auto it = m_trajectories.cbegin(); it != m_trajectories.cend(); ++it)
        if (it->enabled) state.cameras.insert(it.key(), it->camera);
    for (const auto &plot : m_plots)
        if (plot) state.ranges.append({plot, plot->yMinimum(), plot->yMaximum(), plot->normalizeY()});
    if (state.ranges.isEmpty()) return;
    if (m_viewHistory.size() >= 100) m_viewHistory.removeFirst();
    m_viewHistory.append(state);
    if (m_viewChangeDepth > 0) m_viewChangeRecorded = true;
    emit viewHistoryChanged();
}

void AppController::clearViewHistory()
{
    m_viewHistory.clear();
    m_viewChangeDepth = 0;
    m_viewChangeRecorded = false;
    emit viewHistoryChanged();
}

void AppController::undoView()
{
    if (m_viewHistory.isEmpty() || m_loading || sessionInteractionBlocked()
        || m_viewChangeDepth > 0) return;
    QScopedValueRollback<bool> restoring(m_restoringView, true);
    const auto state = m_viewHistory.takeLast();
    applySharedXRange(state.xMinimum, state.xMaximum);
    for (const auto &range : state.ranges)
        if (range.plot && m_plots.contains(range.plot)) {
            range.plot->setNormalizeY(range.normalized);
            range.plot->setYRange(range.minimum, range.maximum);
        }
    for (auto it = state.cameras.cbegin(); it != state.cameras.cend(); ++it) {
        m_trajectories[it.key()].camera = it.value();
        if (it.key() < m_trajectoryPlots.size() && m_trajectoryPlots[it.key()])
            m_trajectoryPlots[it.key()]->setCamera(it.value());
    }
    emit viewHistoryChanged();
}

void AppController::selectSignal(int row)
{
    if (sessionInteractionBlocked()) return;
    if (m_trajectories.value(m_signals->activePlot()).enabled) {
        setTrajectorySignal(m_signals->activePlot(), row, true); return;
    }
    if (row >= 0) m_signals->setChecked(row, true);
    const int plotIndex = m_signals->activePlot();
    if (plotIndex >= 0) refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::toggleSignal(int row)
{
    if (sessionInteractionBlocked()) return;
    const int plotIndex = m_signals->activePlot();
    if (plotIndex < 0 || row < 0) return;
    if (m_trajectories.value(plotIndex).enabled) {
        setTrajectorySignal(plotIndex, row, !plotSignalEnabled(plotIndex, row)); return;
    }
    m_signals->setPlotChecked(plotIndex, row, !m_signals->plotRows(plotIndex).contains(row));
    refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::filterSignals(const QString &text)
{
    if (sessionInteractionBlocked()) return;
    m_signals->setFilter(text);
}

bool AppController::plotSignalEnabled(int plotIndex, int row) const
{
    const auto state = m_trajectories.value(plotIndex);
    if (state.enabled) return state.signalIds.contains(row);
    return m_signals->plotRows(plotIndex).contains(row);
}

QString AppController::signalName(int row) const
{
    return m_signals->nameAt(row);
}

bool AppController::renameSignal(int row, const QString &name)
{
    if (sessionInteractionBlocked()) return false;
    const QString previous = m_signals->nameAt(row);
    if (!m_signals->renameSignal(row, name)) return false;
    if (m_signals->nameAt(row) == previous) return true;
    // Legends read names through the controller, so bump the revision the
    // same way a pen edit does to rebuild their entries.
    notifyPlotBindingsChanged();
    setStatus(QStringLiteral("已重命名信号：%1 → %2")
                  .arg(previous, m_signals->nameAt(row)));
    return true;
}

double AppController::signalTimeOffset(int row) const
{
    const auto snapshot = m_seriesStore->snapshot({row});
    return snapshot.series.isEmpty() ? 0.0 : snapshot.series.first()->timeOffset;
}

bool AppController::resetSignalName(int row)
{
    return renameSignal(row, m_signals->originalNameAt(row));
}

QSet<PlotSeriesId> AppController::timeOffsetRows(int scope, int row, const QString &group) const
{
    QSet<PlotSeriesId> ids;
    for (int id = 0; id < m_signals->sourceCount(); ++id) {
        const QString candidate = m_signals->groupAt(id);
        if ((scope == 0 && id == row)
            || (scope == 1 && !group.isEmpty() && candidate == group)
            || (scope == 2 && !group.isEmpty()
                && (candidate == group || candidate.startsWith(group + QLatin1Char('/')))))
            ids.insert(id);
    }
    return ids;
}

QVariant AppController::timeOffsetForScope(int scope, int row, const QString &group) const
{
    const auto ids = timeOffsetRows(scope, row, group);
    const auto snapshot = m_seriesStore->snapshot(QVector<int>(ids.cbegin(), ids.cend()));
    if (snapshot.series.isEmpty()) return {};
    const double value = snapshot.series.first()->timeOffset;
    for (const auto &series : snapshot.series)
        if (series->timeOffset != value) return {}; // Mixed values: do not display a false zero.
    return value;
}

bool AppController::setTimeOffset(int scope, int row, const QString &group, double seconds)
{
    return applyTimeOffset(scope, row, group, seconds, true);
}

bool AppController::addTimeOffset(int scope, int row, const QString &group, double seconds)
{
    return applyTimeOffset(scope, row, group, seconds, false);
}

bool AppController::applyTimeOffset(int scope, int row, const QString &group, double seconds, bool absolute)
{
    if (m_loading || m_exporting || !qIsFinite(seconds)) return false;
    const auto ids = timeOffsetRows(scope, row, group);
    if (!m_seriesStore->addTimeOffset(ids, seconds, absolute)) {
        setStatus(QStringLiteral("无法应用时间偏移：目标无效或数值超出范围"));
        return false;
    }
    for (int index = 0; index < m_plots.size(); ++index) refreshPlot(index, false);
    notifyPlotBindingsChanged();
    setStatus((absolute ? QStringLiteral("已将 %1 个信号的时间偏移设置为 %2 秒")
                        : QStringLiteral("已对 %1 个信号累加时间偏移 %2 秒"))
                  .arg(ids.size()).arg(seconds, 0, 'g', 15));
    return true;
}

QVariantList AppController::plotSignalRows(int plotIndex) const
{
    QVariantList rows;
    for (int row : m_signals->plotRows(plotIndex)) rows.append(row);
    return rows;
}

void AppController::setSignalPen(int row, const QColor &color, double width, int style)
{
    if (sessionInteractionBlocked()) return;
    if (row < 0 || row >= m_signalColors.size()) return;
    m_signals->setSignalPen(row, color, width, static_cast<Qt::PenStyle>(style));
    m_signalColors[row] = m_signals->signalColor(row);
    const auto generation = m_seriesStore->generation();
    m_seriesStore->updateSeriesPen(row, m_signals->signalColor(row),
                                   m_signals->signalWidth(row),
                                   m_signals->signalStyle(row));
    if (generation == m_seriesStore->generation()) return;
    for (int plotIndex = 0; plotIndex < m_plots.size(); ++plotIndex)
        if (m_signals->plotRows(plotIndex).contains(row)
            || (m_trajectories.value(plotIndex).enabled
                && (m_trajectories.value(plotIndex).axes[0] == row || m_trajectories.value(plotIndex).axes[1] == row || m_trajectories.value(plotIndex).axes[2] == row)))
            refreshPlot(plotIndex, false);
    notifyPlotBindingsChanged();
}

void AppController::revealLegendSignal(int plotIndex, int row)
{
    if (sessionInteractionBlocked()) return;
    if (!plotSignalEnabled(plotIndex, row)) return;
    setActivePlot(plotIndex);
    if (PlotItem *plot = plotAt(plotIndex)) plot->setHighlightedSeries(row);
    emit revealSignalRequested(row);
}

void AppController::moveLegendSignal(int fromPlot, int toPlot, int row)
{
    if (sessionInteractionBlocked()) return;
    if (fromPlot == toPlot || toPlot < 0 || toPlot >= m_plotRows * m_plotColumns
        || !plotSignalEnabled(fromPlot, row)) return;
    if (m_trajectories.value(toPlot).enabled) {
        if (!setTrajectorySignal(toPlot, row, true)) return;
    } else m_signals->setPlotChecked(toPlot, row, true);
    if (m_trajectories.value(fromPlot).enabled) setTrajectorySignal(fromPlot, row, false);
    else m_signals->setPlotChecked(fromPlot, row, false);
    refreshPlot(fromPlot);
    refreshPlot(toPlot, true);
    setActivePlot(toPlot);
    notifyPlotBindingsChanged();
}

void AppController::removeLegendSignal(int plotIndex, int row)
{
    if (sessionInteractionBlocked()) return;
    if (m_trajectories.value(plotIndex).enabled) { setTrajectorySignal(plotIndex, row, false); return; }
    m_signals->setPlotChecked(plotIndex, row, false);
    refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::clearPlotSignals(int plotIndex)
{
    if (sessionInteractionBlocked()) return;
    if (!unbindPlotSignals(plotIndex)) return;
    notifyPlotBindingsChanged();
}

void AppController::clearAllPlotSignals()
{
    if (sessionInteractionBlocked()) return;
    // Unbind every drawn signal but keep loaded files and series data.
    bool changed = false;
    for (int plotIndex = 0; plotIndex < m_plotRows * m_plotColumns; ++plotIndex)
        changed = unbindPlotSignals(plotIndex) || changed;
    if (!changed) return;
    notifyPlotBindingsChanged();
    setStatus(QStringLiteral("已清除所有信号"));
}

bool AppController::unbindPlotSignals(int plotIndex)
{
    const QVector<int> rows = m_signals->plotRows(plotIndex);
    bool trajectoryChanged = false;
    auto &trajectory = m_trajectories[plotIndex];
    if (trajectory.enabled && (!trajectory.signalIds.isEmpty() || trajectory.axes != std::array<int, 3>{{-1, -1, -1}})) {
        trajectory.axes = {{-1, -1, -1}}; trajectory.signalIds.clear(); trajectoryChanged = true;
    }
    if (trajectory.enabled ? !trajectoryChanged : rows.isEmpty()) return false;
    if (!trajectory.enabled) for (int row : rows) m_signals->setPlotChecked(plotIndex, row, false);
    refreshPlot(plotIndex);
    return true;
}

void AppController::fitPlotY(int plotIndex)
{
    if (sessionInteractionBlocked()) return;
    if (PlotItem *plot = plotAt(plotIndex)) plot->fitY();
}

PlotItem *AppController::plotAt(int index) const
{
    return index >= 0 && index < m_plots.size() ? m_plots.at(index).data() : nullptr;
}

void AppController::applySharedXRange(double xMinimum, double xMaximum, PlotItem *except)
{
    if (!qIsFinite(xMinimum) || !qIsFinite(xMaximum) || xMaximum <= xMinimum
        || !qIsFinite(xMaximum - xMinimum)) return;
    m_sharedXMinimum = xMinimum;
    m_sharedXMaximum = xMaximum;
    m_syncingRanges = true;
    for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
        if (plot && plot != except) plot->setXRange(xMinimum, xMaximum);
    m_syncingRanges = false;
    syncTrajectoryCursors();
}

void AppController::syncCursorsFrom(PlotItem *source, PlotItem *target)
{
    target->restoreCursorState(source->cursorMode(), source->cursorX1(), source->cursorX2());
}

void AppController::attachPlot(QObject *plot, int index)
{
    QScopedValueRollback<bool> restoring(m_restoringView, true);
    auto *item = qobject_cast<PlotItem *>(plot);
    if (!item || index < 0 || index >= m_plotRows * m_plotColumns) return;
    if (!m_applyingSession && index < m_plots.size() && m_plots.at(index))
        cachePlotView(m_plots.at(index));
    const std::optional<SessionPlot> view = m_plotViews.contains(index)
        ? std::optional<SessionPlot>(m_plotViews.value(index)) : std::nullopt;
    if (index < m_plots.size() && m_plots[index] == item) {
        item->setSeriesStore(m_seriesStore);
        refreshPlot(index, !view.has_value());
        if (view) applyPlotView(item, *view);
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
    connect(item, &PlotItem::seriesClicked, this, [this, index](int id) {
        revealLegendSignal(index, id);
    });
    connect(item, &PlotItem::rangeAboutToChange, this, &AppController::recordViewChange);
    connect(item, &PlotItem::viewInteractionStarted, this, &AppController::beginViewChange);
    connect(item, &PlotItem::viewInteractionFinished, this, &AppController::endViewChange);
    connect(item, &PlotItem::rangeChanged, this,
            [this, item](double xmin, double xmax, double, double) {
                cachePlotView(item);
                if (m_syncingRanges || sessionInteractionBlocked()) return;
                applySharedXRange(xmin, xmax, item);
            });
    connect(item, &PlotItem::cursorChanged, this, [this, item]() {
        if (m_syncingCursors || sessionInteractionBlocked()) return;
        m_sessionCursor = {item->cursorMode(), item->cursorX1(), item->cursorX2()};
        m_haveCursorState = true;
        m_syncingCursors = true;
        for (const QPointer<PlotItem> &other : std::as_const(m_plots))
            if (other && other != item) syncCursorsFrom(item, other);
        m_syncingCursors = false;
        syncTrajectoryCursors();
    });
    connect(item, &PlotItem::normalizeYChanged, this, [this, item]() { cachePlotView(item); });
    connect(item, &PlotItem::lineWidthChanged, this, [this, item]() { cachePlotView(item); });
    refreshPlot(index, !view.has_value());
    if (m_haveCursorState) {
        item->restoreCursorState(m_sessionCursor.mode, m_sessionCursor.x1, m_sessionCursor.x2);
    } else {
        m_sessionCursor = {item->cursorMode(), item->cursorX1(), item->cursorX2()};
        m_haveCursorState = true;
    }
    if (view) applyPlotView(item, *view);
    cachePlotView(item);
}

void AppController::detachPlot(QObject *plot, int index)
{
    auto *item = qobject_cast<PlotItem *>(plot);
    if (!item || index < 0 || index >= m_plots.size()) return;
    if (m_plots.at(index) != item) return;
    cachePlotView(item);
    disconnect(item, nullptr, this, nullptr);
    m_plots[index].clear();
}

void AppController::setLayout(int rows, int columns)
{
    if (sessionInteractionBlocked()) return;
    const int normalizedRows = qBound(1, rows, 8);
    const int normalizedColumns = qBound(1, columns, 8);
    if (m_plotRows == normalizedRows && m_plotColumns == normalizedColumns)
        return;
    clearViewHistory();
    // A numeric QML Repeater retains delegates whose indices still exist.
    // Preserve those attachments and leave new slots empty for attachPlot().
    const int plotCount = normalizedRows * normalizedColumns;
    for (auto it = m_plotViews.begin(); it != m_plotViews.end();) {
        if (it.key() >= plotCount) it = m_plotViews.erase(it); else ++it;
    }
    for (int index = plotCount; index < m_plots.size(); ++index)
        if (m_plots.at(index))
            disconnect(m_plots.at(index), nullptr, this, nullptr);
    m_plots.resize(plotCount);
    for (int i = plotCount; i < m_trajectoryPlots.size(); ++i)
        if (m_trajectoryPlots[i]) disconnect(m_trajectoryPlots[i], nullptr, this, nullptr);
    m_trajectoryPlots.resize(plotCount);
    for (auto it = m_trajectories.begin(); it != m_trajectories.end();)
        if (it.key() >= plotCount) it = m_trajectories.erase(it); else ++it;

    ++m_plotStateRevision;
    const int previousActivePlot = m_signals->activePlot();
    // The maximized subplot may not exist in the new grid.
    setSoloPlot(-1);
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
    if (sessionInteractionBlocked()) return;
    if (index == m_signals->activePlot()) return;
    m_signals->setActivePlot(index);
    syncSignalSelection();
    emit activePlotChanged();
}

void AppController::setSoloPlot(int index)
{
    if (sessionInteractionBlocked()) return;
    const int plotCount = m_plotRows * m_plotColumns;
    const int normalized = index >= 0 && index < plotCount ? index : -1;
    if (m_soloPlotIndex == normalized) return;
    m_soloPlotIndex = normalized;
    emit soloPlotChanged();
}

void AppController::setCursorMode(int mode)
{
    if (sessionInteractionBlocked()) return;
    const int index = m_soloPlotIndex >= 0 ? m_soloPlotIndex : activePlotIndex();
    if (index < 0 || index >= m_plots.size() || !m_plots.at(index)) return;
    // Initialize in the selected view before synchronizing other subplots.
    m_plots.at(index)->setCursorMode(mode);
}

void AppController::fitAllPlots()
{
    fitPlots(true, true, true);
}

void AppController::fitPlots(bool fitX, bool fitY, bool allPlots)
{
    if (sessionInteractionBlocked()) return;
    beginViewChange();
    bool includeTimePlot = false;
    for (int i = 0; i < m_plotRows * m_plotColumns; ++i) if (fitScopeIncludes(i, allPlots)) {
        if (m_trajectories.value(i).enabled) {
            if ((fitX || fitY) && i < m_trajectoryPlots.size() && m_trajectoryPlots[i]) m_trajectoryPlots[i]->fitView();
        } else includeTimePlot = true;
    }
    if (fitX && includeTimePlot) {
        const auto bounds = PlotSeriesStore::timeBounds(
                    m_seriesStore->snapshot(fitSourceRows(allPlots)));
        double xmin = 0, xmax = 10;
        if (bounds) {
            const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5);
            if (range) { xmin = range->first; xmax = range->second; }
            else { xmin = m_sharedXMinimum; xmax = m_sharedXMaximum; }
        }
        // The time axis is shared, so the fitted range always lands on every
        // subplot; only the set of signals that defines it varies.
        if (xmin != m_sharedXMinimum || xmax != m_sharedXMaximum) recordViewChange();
        applySharedXRange(xmin, xmax);
    }
    if (fitY) {
        for (int index = 0; index < m_plots.size(); ++index) {
            if (!m_plots.at(index)) continue;
            if (!fitScopeIncludes(index, allPlots) || m_trajectories.value(index).enabled) continue;
            m_plots.at(index)->fitY();
        }
    }
    endViewChange();
}

// A maximized subplot is the only one the user can see, so it always wins over
// an "all subplots" request; otherwise "current" means the active subplot.
bool AppController::fitScopeIncludes(int plotIndex, bool allPlots) const
{
    if (m_soloPlotIndex >= 0) return plotIndex == m_soloPlotIndex;
    return allPlots || plotIndex == activePlotIndex();
}

QVector<PlotSeriesId> AppController::fitSourceRows(bool allPlots) const
{
    QVector<PlotSeriesId> ids;
    for (int index = 0; index < m_plotRows * m_plotColumns; ++index) {
        if (!fitScopeIncludes(index, allPlots) || m_trajectories.value(index).enabled) continue;
        for (int id : m_signals->plotRows(index))
            if (!ids.contains(id)) ids.append(id);
    }
    return ids;
}

void AppController::refreshPlot(int index, bool fitY)
{
    refreshTrajectory(index);
    if (index < 0 || index >= m_plots.size() || !m_plots.at(index)) return;
    PlotItem *plot = m_plots.at(index);
    QVector<int> sortedRows = m_signals->plotRows(index);
    if (m_trajectories.value(index).enabled) {
        const auto axes = m_trajectories.value(index).axes;
        sortedRows = QVector<int>(axes.cbegin(), axes.cend());
    }
    QVector<PlotSeriesId> visibleIds;
    visibleIds.reserve(sortedRows.size());
    for (int row : sortedRows)
        if (row >= 0 && row < m_signalColors.size()) visibleIds.append(row);
    plot->setVisibleSeries(visibleIds);
    if (m_trajectories.value(index).enabled) return;
    if (fitY && !m_initialSignalFitDone && !visibleIds.isEmpty() && !m_applyingSession) {
        m_initialSignalFitDone = true;
        // fitView emits rangeChanged, synchronizing the first time fit to all plots.
        plot->fitView();
        return;
    }
    if (fitY) plot->fitY();
}
