// AppController: subplot bindings, legend actions, layout and view fitting.
#include "appcontroller.h"
#include "plotitem.h"

#include <utility>
#include <QtMath>

void AppController::selectSignal(int row)
{
    if (sessionInteractionBlocked()) return;
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
    m_signals->setPlotChecked(plotIndex, row, !m_signals->plotRows(plotIndex).contains(row));
    refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::filterSignals(const QString &text)
{
    if (sessionInteractionBlocked()) return;
    m_signals->setFilter(text);
}

void AppController::setAllSignalsChecked(bool checked)
{
    if (sessionInteractionBlocked()) return;
    m_signals->setAllChecked(checked);
    const int plotIndex = m_signals->activePlot();
    if (plotIndex >= 0) refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

bool AppController::plotSignalEnabled(int plotIndex, int row) const
{
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
    m_seriesStore->updateSeriesPen(row, m_signals->signalColor(row),
                                   m_signals->signalWidth(row),
                                   m_signals->signalStyle(row));
    for (int plotIndex = 0; plotIndex < m_plots.size(); ++plotIndex)
        if (m_signals->plotRows(plotIndex).contains(row))
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
    m_signals->setPlotChecked(toPlot, row, true);
    m_signals->setPlotChecked(fromPlot, row, false);
    refreshPlot(fromPlot, false);
    refreshPlot(toPlot, true);
    setActivePlot(toPlot);
    notifyPlotBindingsChanged();
}

void AppController::removeLegendSignal(int plotIndex, int row)
{
    if (sessionInteractionBlocked()) return;
    m_signals->setPlotChecked(plotIndex, row, false);
    refreshPlot(plotIndex, false);
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
    if (rows.isEmpty()) return false;
    for (int row : rows) m_signals->setPlotChecked(plotIndex, row, false);
    refreshPlot(plotIndex, false);
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
    m_sharedXMinimum = xMinimum;
    m_sharedXMaximum = xMaximum;
    m_syncingRanges = true;
    for (const QPointer<PlotItem> &plot : std::as_const(m_plots))
        if (plot && plot != except) plot->setXRange(xMinimum, xMaximum);
    m_syncingRanges = false;
}

void AppController::syncCursorsFrom(PlotItem *source, PlotItem *target)
{
    target->restoreCursorState(source->cursorMode(), source->cursorX1(), source->cursorX2());
}

void AppController::attachPlot(QObject *plot, int index)
{
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

void AppController::fitAllPlots()
{
    fitPlots(true, true, true);
}

void AppController::fitPlots(bool fitX, bool fitY, bool allPlots)
{
    if (sessionInteractionBlocked()) return;
    if (fitX) {
        const auto bounds = PlotSeriesStore::timeBounds(
                    m_seriesStore->snapshot(fitSourceRows(allPlots)));
        double xmin = 0, xmax = 10;
        if (bounds) {
            const double span = bounds->second - bounds->first;
            const double padding = span > 0 ? span * .02 : .5;
            xmin = bounds->first - padding;
            xmax = bounds->second + padding;
        }
        // The time axis is shared, so the fitted range always lands on every
        // subplot; only the set of signals that defines it varies.
        applySharedXRange(xmin, xmax);
    }
    if (fitY) {
        for (int index = 0; index < m_plots.size(); ++index) {
            if (!m_plots.at(index)) continue;
            if (!fitScopeIncludes(index, allPlots)) continue;
            m_plots.at(index)->fitY();
        }
    }
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
        if (!fitScopeIncludes(index, allPlots)) continue;
        for (int id : m_signals->plotRows(index))
            if (!ids.contains(id)) ids.append(id);
    }
    return ids;
}

void AppController::refreshPlot(int index, bool fitY)
{
    if (index < 0 || index >= m_plots.size() || !m_plots.at(index)) return;
    PlotItem *plot = m_plots.at(index);
    const QVector<int> sortedRows = m_signals->plotRows(index);
    QVector<PlotSeriesId> visibleIds;
    visibleIds.reserve(sortedRows.size());
    for (int row : sortedRows)
        if (row >= 0 && row < m_signalColors.size()) visibleIds.append(row);
    plot->setVisibleSeries(visibleIds);
    if (fitY) plot->fitY();
}
