// AppController: subplot bindings, legend actions, layout and view fitting.
#include "appcontroller.h"
#include "plotitem.h"

#include <utility>

void AppController::selectSignal(int row)
{
    if (row >= 0) m_signals->setChecked(row, true);
    const int plotIndex = m_signals->activePlot();
    if (plotIndex >= 0) refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::toggleSignal(int row)
{
    const int plotIndex = m_signals->activePlot();
    if (plotIndex < 0 || row < 0) return;
    m_signals->setPlotChecked(plotIndex, row, !m_signals->plotRows(plotIndex).contains(row));
    refreshPlot(plotIndex);
    notifyPlotBindingsChanged();
}

void AppController::filterSignals(const QString &text)
{
    m_signals->setFilter(text);
}

void AppController::setAllSignalsChecked(bool checked)
{
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

QVariantList AppController::plotSignalRows(int plotIndex) const
{
    QVariantList rows;
    for (int row : m_signals->plotRows(plotIndex)) rows.append(row);
    return rows;
}

void AppController::setSignalPen(int row, const QColor &color, double width, int style)
{
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
    if (!plotSignalEnabled(plotIndex, row)) return;
    setActivePlot(plotIndex);
    if (PlotItem *plot = plotAt(plotIndex)) plot->setHighlightedSeries(row);
    emit revealSignalRequested(row);
}

void AppController::moveLegendSignal(int fromPlot, int toPlot, int row)
{
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
    m_signals->setPlotChecked(plotIndex, row, false);
    refreshPlot(plotIndex, false);
    notifyPlotBindingsChanged();
}

void AppController::clearPlotSignals(int plotIndex)
{
    if (!unbindPlotSignals(plotIndex)) return;
    notifyPlotBindingsChanged();
}

void AppController::clearAllPlotSignals()
{
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
    target->setCursorMode(source->cursorMode());
    if (source->cursorMode() == PlotItem::NoCursor) return;
    target->setCursorPosition(source->cursorX1(), 1);
    if (source->cursorMode() == PlotItem::DoubleCursor)
        target->setCursorPosition(source->cursorX2(), 2);
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
                applySharedXRange(xmin, xmax, item);
            });
    connect(item, &PlotItem::cursorChanged, this, [this, item]() {
        if (m_syncingCursors) return;
        m_syncingCursors = true;
        for (const QPointer<PlotItem> &other : std::as_const(m_plots))
            if (other && other != item) syncCursorsFrom(item, other);
        m_syncingCursors = false;
    });
    refreshPlot(index);
    for (const QPointer<PlotItem> &source : std::as_const(m_plots)) {
        if (!source || source == item) continue;
        syncCursorsFrom(source, item);
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
    fitPlots(true, true, true);
}

void AppController::fitPlots(bool fitX, bool fitY, bool allPlots)
{
    if (fitX) {
        QVector<PlotSeriesId> ids;
        for (int index = 0; index < m_plotRows * m_plotColumns; ++index)
            for (int id : m_signals->plotRows(index))
                if (!ids.contains(id)) ids.append(id);
        const auto bounds = PlotSeriesStore::timeBounds(m_seriesStore->snapshot(ids));
        double xmin = 0, xmax = 10;
        if (bounds) {
            const double span = bounds->second - bounds->first;
            const double padding = span > 0 ? span * .02 : .5;
            xmin = bounds->first - padding;
            xmax = bounds->second + padding;
        }
        applySharedXRange(xmin, xmax);
    }
    if (fitY) {
        for (int index = 0; index < m_plots.size(); ++index) {
            if (!m_plots.at(index) || (!allPlots && index != activePlotIndex())) continue;
            m_plots.at(index)->fitY();
        }
    }
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
