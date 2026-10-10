// PlotItem lifecycle, shared snapshots, view ranges and axes.
#include "plotitem.h"
#include "render/plotaxisutils.h"
#include <QMutexLocker>
#include <QtMath>
#include <limits>
#include <utility>

PlotItem::PlotItem(QQuickItem *parent) : QQuickItem(parent)
{
    m_lodScheduler = new PlotLodScheduler(this);
    connect(m_lodScheduler, &PlotLodScheduler::ready, this, [this] {
        { QMutexLocker lock(&m_dataMutex); m_lodResult = m_lodScheduler->result(); }
        emit lodChanged();
        update();
    });
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setClip(true);
    rebuildTicksLocked();
}
void PlotItem::requestLod()
{
    if (!m_curveRenderingEnabled) return;
    PlotSeriesSnapshot snapshot;
    LodRequestKey key;
    {
        QMutexLocker lock(&m_dataMutex);
        snapshot = m_seriesSnapshot;
        key = {snapshot.generation, snapshot.orderedIds, m_xMinimum, m_xMaximum,
               qCeil(qBound(64.0, qIsFinite(width()) ? width() : 64.0, 4096.0)), 3};
    }
    m_lodScheduler->request(snapshot, key);
    { QMutexLocker lock(&m_dataMutex); m_lodResult = m_lodScheduler->result(); }
    emit lodChanged();
}
void PlotItem::setCurveRenderingEnabled(bool enabled)
{
    if (m_curveRenderingEnabled == enabled) return;
    m_curveRenderingEnabled = enabled;
    if (enabled) requestLod();
    else {
        m_lodScheduler->cancel();
        { QMutexLocker lock(&m_dataMutex); m_lodResult.reset(); }
        emit lodChanged();
    }
    update();
}
int PlotItem::highlightedSeries() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_highlightedSeries;
}
void PlotItem::setHighlightedSeries(int id)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (!m_visibleSeries.contains(id)) id = -1;
        if (id == m_highlightedSeries) return;
        m_highlightedSeries = id;
    }
    emit highlightedSeriesChanged();
    update();
}
double PlotItem::xMinimum() const { QMutexLocker lock(&m_dataMutex); return m_xMinimum; }
double PlotItem::xMaximum() const { QMutexLocker lock(&m_dataMutex); return m_xMaximum; }
double PlotItem::yMinimum() const { QMutexLocker lock(&m_dataMutex); return m_yMinimum; }
double PlotItem::yMaximum() const { QMutexLocker lock(&m_dataMutex); return m_yMaximum; }
double PlotItem::lineWidth() const { QMutexLocker lock(&m_dataMutex); return m_lineWidth; }
int PlotItem::cursorMode() const { QMutexLocker lock(&m_dataMutex); return m_cursorMode; }
double PlotItem::cursorX1() const { QMutexLocker lock(&m_dataMutex); return m_cursorX1; }
double PlotItem::cursorX2() const { QMutexLocker lock(&m_dataMutex); return m_cursorX2; }
double PlotItem::cursorDeltaT() const { QMutexLocker lock(&m_dataMutex); return m_cursorX2 - m_cursorX1; }
QVariantList PlotItem::xTicks() const { QMutexLocker lock(&m_dataMutex); return m_xTicks; }
QVariantList PlotItem::yTicks() const { QMutexLocker lock(&m_dataMutex); return m_yTicks; }
QVariantList PlotItem::cursorReadouts() const { QMutexLocker lock(&m_dataMutex); return m_cursorReadouts; }
void PlotItem::setLineWidth(double width)
{
    const double v = qBound(1.0, width, 12.0);
    {
        QMutexLocker lock(&m_dataMutex);
        if (qFuzzyCompare(v, m_lineWidth)) return;
        m_lineWidth = v;
    }
    emit lineWidthChanged();
    update();
}
void PlotItem::setSeriesStore(const std::shared_ptr<const PlotSeriesStore> &store)
{
    {
        QMutexLocker lock(&m_dataMutex);
        m_seriesStore = store;
        refreshSnapshotLocked();
        updateCursorValuesLocked();
    }
    requestLod();
    emit cursorValuesChanged();
    update();
}

void PlotItem::setVisibleSeries(const QVector<PlotSeriesId> &orderedIds)
{
    QVector<PlotSeriesId> uniqueIds;
    uniqueIds.reserve(orderedIds.size());
    for (PlotSeriesId id : orderedIds) {
        if (id >= 0 && !uniqueIds.contains(id))
            uniqueIds.append(id);
    }
    {
        QMutexLocker lock(&m_dataMutex);
        m_visibleSeries = std::move(uniqueIds);
        refreshSnapshotLocked();
        updateCursorValuesLocked();
    }
    setHighlightedSeries(highlightedSeries());
    requestLod();
    emit cursorValuesChanged();
    update();
}

QVector<PlotSeriesId> PlotItem::visibleSeriesIds() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_visibleSeries;
}

void PlotItem::refreshSnapshotLocked()
{
    m_seriesSnapshot = m_seriesStore
            ? m_seriesStore->snapshot(m_visibleSeries) : PlotSeriesSnapshot{};
    rebuildNormalizationLocked();
}

void PlotItem::rebuildNormalizationLocked()
{
    m_normalization.clear();
    if (!m_normalizeY) return;
    for (const PlotSeriesDataPtr &series : std::as_const(m_seriesSnapshot.series)) {
        if (!series) continue;
        double minimum = qInf(), maximum = -qInf();
        for (double value : series->values) {
            if (!qIsFinite(value)) continue;
            minimum = qMin(minimum, value);
            maximum = qMax(maximum, value);
        }
        if (!qIsFinite(minimum)) continue;
        // A constant series is drawn at 0.5.
        m_normalization.insert(series->id, {minimum, maximum > minimum ? maximum - minimum : 0.0});
    }
}

double PlotItem::normalizedYLocked(PlotSeriesId id, double y) const
{
    if (!m_normalizeY || !qIsFinite(y)) return y;
    const auto it = m_normalization.constFind(id);
    if (it == m_normalization.cend()) return y;
    return it->second > 0.0 ? (y - it->first) / it->second : 0.5;
}

bool PlotItem::normalizeY() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_normalizeY;
}

int PlotItem::blendMode() const
{
    QMutexLocker lock(&m_dataMutex);
    return m_blendMode;
}

void PlotItem::setBlendMode(int mode)
{
    const int normalized = mode == AmplitudeLayers ? AmplitudeLayers : OpaqueBlend;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_blendMode == normalized) return;
        m_blendMode = normalized;
    }
    emit blendModeChanged();
    update();
}

void PlotItem::setNormalizeY(bool enabled)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_normalizeY == enabled) return;
    }
    emit viewInteractionStarted();
    emit rangeAboutToChange();
    {
        QMutexLocker lock(&m_dataMutex);
        m_normalizeY = enabled;
        rebuildNormalizationLocked();
        updateCursorValuesLocked();
    }
    emit normalizeYChanged();
    emit cursorValuesChanged();
    fitY();
    emit viewInteractionFinished();
    update();
}

void PlotItem::fitView()
{
    std::optional<QPair<double, double>> bounds;
    { QMutexLocker lock(&m_dataMutex); bounds = PlotSeriesStore::timeBounds(m_seriesSnapshot); }
    if (!bounds) { setRange(0, 10, 0, 1); return; }
    const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5);
    if (range) setXRange(range->first, range->second);
    fitY();
}
void PlotItem::fitY()
{
    double xmin, xmax;
    std::optional<PlotBounds> bounds;
    bool normalized = false;
    {
        QMutexLocker lock(&m_dataMutex);
        xmin = m_xMinimum; xmax = m_xMaximum;
        normalized = m_normalizeY && !m_normalization.isEmpty();
        bounds = PlotSeriesStore::bounds(m_seriesSnapshot, xmin, xmax);
    }
    if (normalized) { setRange(xmin, xmax, -.05, 1.05); return; }
    if (!bounds) { setRange(xmin, xmax, 0, 1); return; }
    const auto range = paddedPlotRange(bounds->yMinimum, bounds->yMaximum, .05,
                                       bounds->yMinimum == 0 ? .5 : qMax(std::abs(bounds->yMinimum) * .05,
                                           std::numeric_limits<double>::denorm_min()));
    if (range) setRange(xmin, xmax, range->first, range->second);
}
void PlotItem::zoomAxis(int axis, double fraction, double steps)
{
    if ((axis != 0 && axis != 1) || !qIsFinite(fraction) || !qIsFinite(steps) || steps == 0) return;
    const double factor = qPow(.85, qBound(-20.0, steps, 20.0));
    double xmin = xMinimum(), xmax = xMaximum(), ymin = yMinimum(), ymax = yMaximum();
    // `fraction` is the pointer position inside the axis gutter: 0..1 left to
    // right for X, and top to bottom for Y (same convention as
    // PlotAxisArea.fraction() and applyAxisSelection). Anchoring the zoom
    // there keeps the data value under the cursor stationary.
    const double position = qBound(0.0, fraction, 1.0);
    if (axis == 0) {
        const double anchor = xmin + position * (xmax - xmin);
        xmin = anchor + (xmin - anchor) * factor;
        xmax = anchor + (xmax - anchor) * factor;
    } else {
        const double anchor = ymax - position * (ymax - ymin);
        ymin = anchor + (ymin - anchor) * factor;
        ymax = anchor + (ymax - anchor) * factor;
    }
    emit activated();
    setRange(xmin, xmax, ymin, ymax);
}
void PlotItem::setXRange(double xmin, double xmax)
{
    double ymin, ymax;
    { QMutexLocker lock(&m_dataMutex); ymin = m_yMinimum; ymax = m_yMaximum; }
    setRange(xmin, xmax, ymin, ymax);
}
void PlotItem::setYRange(double ymin, double ymax)
{
    double xmin, xmax;
    { QMutexLocker lock(&m_dataMutex); xmin = m_xMinimum; xmax = m_xMaximum; }
    setRange(xmin, xmax, ymin, ymax);
}

void PlotItem::rebuildTicksLocked()
{
    m_xTicks = makePlotAxisTicks(m_xMinimum, m_xMaximum, qBound(2, int(width() / 85), 20));
    m_yTicks = makePlotAxisTicks(m_yMinimum, m_yMaximum, qBound(2, int(height() / 40), 20));
}

void PlotItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    // Axis label widths also affect this item's geometry. Defer notification
    // until QML has finished its layout bindings, and emit only on real changes.
    QMetaObject::invokeMethod(this, [this]() {
        bool changed;
        {
            QMutexLocker lock(&m_dataMutex);
            const auto oldX = m_xTicks, oldY = m_yTicks;
            rebuildTicksLocked();
            changed = oldX != m_xTicks || oldY != m_yTicks;
        }
        if (changed) emit axisTicksChanged();
    }, Qt::QueuedConnection);
    requestLod();
    update();
}

void PlotItem::setRange(double xmin,double xmax,double ymin,double ymax)
{
    if (!qIsFinite(xmin) || !qIsFinite(xmax) || !qIsFinite(ymin) || !qIsFinite(ymax)
        || xmax <= xmin || ymax <= ymin
        || !qIsFinite(xmax - xmin) || !qIsFinite(ymax - ymin))
        return;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_xMinimum == xmin && m_xMaximum == xmax && m_yMinimum == ymin && m_yMaximum == ymax) return;
    }
    emit rangeAboutToChange();
    {
        QMutexLocker lock(&m_dataMutex);
        m_xMinimum = xmin;
        m_xMaximum = xmax;
        m_yMinimum = ymin;
        m_yMaximum = ymax;
        rebuildTicksLocked();
        updateCursorValuesLocked();
    }
    emit cursorValuesChanged();
    requestLod();
    emit viewChanged();
    emit rangeChanged(xmin, xmax, ymin, ymax);
    emit axisTicksChanged();
    update();
}
