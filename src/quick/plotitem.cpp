#include "plotitem.h"
#include "render/plotgeometrybuilder.h"
#include <QMouseEvent>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QWheelEvent>
#include <QMutexLocker>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace {

// Persistent scene-graph state.  The render thread owns this object after it
// is returned from updatePaintNode(); keeping the nodes alive lets us resize
// and refill their vertex buffers instead of allocating a new node tree for
// every pan/zoom/cursor update.
struct PlotRoot final : QSGNode {
    QVector<QSGGeometryNode *> lineNodes;
    std::shared_ptr<const LodResult> curveResult;
    QVector<double> curveView;

    QSGGeometryNode *cursorNode = nullptr;
    QSGGeometryNode *cursorNode2 = nullptr;
};

static QSGGeometryNode *createLineNode(PlotRoot *root)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    if (root->cursorNode) root->insertChildNodeBefore(node, root->cursorNode);
    else root->appendChildNode(node);
    root->lineNodes.append(node);
    return node;
}

static void uploadLineNode(QSGGeometryNode *node,
                           const GeometrySegment &segment)
{
    auto *geometry = node->geometry();
    geometry->setDrawingMode(segment.triangleList
                             ? QSGGeometry::DrawTriangles
                             : QSGGeometry::DrawTriangleStrip);
    geometry->allocate(segment.vertices.size());
    auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
    for (int i = 0; i < segment.vertices.size(); ++i) {
        const QPointF point = segment.vertices.at(i);
        vertices[i].set(static_cast<float>(point.x()),
                        static_cast<float>(point.y()));
    }
    geometry->markVertexDataDirty();
    if (auto *material = static_cast<QSGFlatColorMaterial *>(node->material()))
        material->setColor(segment.color);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

std::optional<double> adjacentRawX(const PlotSeriesSnapshot &snapshot,
                                   double currentX, int direction)
{
    if (!qIsFinite(currentX) || direction == 0)
        return std::nullopt;

    const bool forward = direction > 0;
    std::optional<double> best;
    const auto consider = [&](double x) {
        if (!qIsFinite(x) || (forward ? x <= currentX : x >= currentX))
            return;
        if (!best || (forward ? x < *best : x > *best))
            best = x;
    };

    for (const PlotSeriesDataPtr &series : snapshot.series) {
        if (!series) continue;
        const qsizetype count = series->sampleCount();
        if (series->monotonicTime) {
            qsizetype lo = 0;
            qsizetype hi = count;
            while (lo < hi) {
                const qsizetype mid = lo + (hi - lo) / 2;
                const double x = series->pointAt(mid).x();
                if (forward ? x <= currentX : x < currentX)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (forward) {
                for (qsizetype i = lo; i < count; ++i) {
                    const double x = series->pointAt(i).x();
                    if (qIsFinite(x)) { consider(x); break; }
                }
            } else {
                for (qsizetype i = lo - 1; i >= 0; --i) {
                    const double x = series->pointAt(i).x();
                    if (qIsFinite(x)) { consider(x); break; }
                }
            }
        } else {
            for (qsizetype i = 0; i < count; ++i)
                consider(series->pointAt(i).x());
        }
    }
    return best;
}

} // namespace

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
    setAcceptHoverEvents(true);
    setClip(true);
    rebuildTicksLocked();
}
void PlotItem::requestLod()
{
    PlotSeriesSnapshot snapshot;
    LodRequestKey key;
    {
        QMutexLocker lock(&m_dataMutex);
        snapshot = m_seriesSnapshot;
        key = {snapshot.generation, snapshot.orderedIds, m_xMinimum, m_xMaximum,
               qBound(64, int(qCeil(width())), 4096), 1};
    }
    m_lodScheduler->request(snapshot, key);
    { QMutexLocker lock(&m_dataMutex); m_lodResult = m_lodScheduler->result(); }
    emit lodChanged();
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
bool PlotItem::cursorEnabled() const { QMutexLocker lock(&m_dataMutex); return m_cursorEnabled; }
double PlotItem::cursorX() const { QMutexLocker lock(&m_dataMutex); return m_cursorX; }
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
void PlotItem::setCursorEnabled(bool enabled) { setCursorMode(enabled ? SingleCursor : NoCursor); }
void PlotItem::setCursorMode(int mode)
{
    const int normalized = qBound(static_cast<int>(NoCursor), mode, static_cast<int>(DoubleCursor));
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == normalized) return;
        const bool wasDisabled = m_cursorMode == NoCursor;
        m_cursorMode = normalized;
        m_cursorEnabled = normalized != NoCursor;
        if (wasDisabled && m_cursorEnabled) {
            const double span = qMax(m_xMaximum - m_xMinimum, 1e-12);
            // Snap the initial positions to raw samples like an interactive drag would.
            m_cursorX1 = nearestRawX(m_xMinimum + span * .25);
            m_cursorX2 = nearestRawX(m_xMinimum + span * .75);
        }
        m_cursorX = m_cursorX1;
        updateCursorValuesLocked();
        rebuildTicksLocked();
    }
    emit cursorChanged();
    emit cursorDeltaTChanged();
    emit cursorValuesChanged();
    update();
}
void PlotItem::setCursorX(double x, int cursorIndex)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor) return;
        const double clamped = qBound(m_xMinimum, x, m_xMaximum);
        const double snapped = nearestRawX(clamped);
        if ((cursorIndex == 2 ? m_cursorX2 : m_cursorX1) == snapped) return;
        if (cursorIndex == 2) m_cursorX2 = snapped;
        else { m_cursorX1 = snapped; m_cursorX = snapped; }
        m_cursorX = cursorIndex == 2 ? m_cursorX2 : m_cursorX1;
        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}
void PlotItem::setCursorPosition(double x, int cursorIndex)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor) return;
        const double clamped = qBound(m_xMinimum, x, m_xMaximum);
        if ((cursorIndex == 2 ? m_cursorX2 : m_cursorX1) == clamped) return;
        if (cursorIndex == 2) m_cursorX2 = clamped;
        else { m_cursorX1 = clamped; m_cursorX = clamped; }
        m_cursorX = cursorIndex == 2 ? m_cursorX2 : m_cursorX1;
        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}

void PlotItem::stepCursor(int direction, int cursorIndex)
{
    if (direction == 0 || cursorIndex < 0 || cursorIndex > 2)
        return;

    bool changed = false;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_cursorMode == NoCursor)
            return;

        const int lastTarget = m_cursorMode == DoubleCursor ? 2 : 1;
        for (int target = 1; target <= lastTarget; ++target) {
            if (cursorIndex != 0 && target != cursorIndex)
                continue;
            const double current = target == 2 ? m_cursorX2 : m_cursorX1;
            const auto next = adjacentRawX(m_seriesSnapshot, current, direction);
            if (!next)
                continue;
            const double clamped = qBound(m_xMinimum, *next, m_xMaximum);
            if (clamped == current)
                continue;
            if (target == 2)
                m_cursorX2 = clamped;
            else
                m_cursorX1 = clamped;
            m_cursorX = clamped;
            changed = true;
        }

        if (!changed)
            return;
        updateCursorValuesLocked();
    }

    emit cursorChanged();
    emit cursorDeltaTChanged();
    emit cursorValuesChanged();
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
    m_cursorValues.resize(m_seriesSnapshot.series.size());
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

void PlotItem::setNormalizeY(bool enabled)
{
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_normalizeY == enabled) return;
        m_normalizeY = enabled;
        rebuildNormalizationLocked();
        updateCursorValuesLocked();
    }
    emit normalizeYChanged();
    emit cursorValuesChanged();
    fitY();
    update();
}

void PlotItem::fitView()
{
    std::optional<QPair<double, double>> bounds;
    { QMutexLocker lock(&m_dataMutex); bounds = PlotSeriesStore::timeBounds(m_seriesSnapshot); }
    if (!bounds) { setRange(0, 10, 0, 1); return; }
    const double span = bounds->second - bounds->first;
    const double padding = span > 0 ? span * .02 : .5;
    setXRange(bounds->first - padding, bounds->second + padding);
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
    const double span = bounds->yMaximum - bounds->yMinimum;
    const double padding = span == 0 ? (bounds->yMinimum == 0 ? .5 : .05) : span * .05;
    setRange(xmin, xmax, bounds->yMinimum - padding, bounds->yMaximum + padding);
}
void PlotItem::zoomAxis(int axis, double fraction, double steps)
{
    if ((axis != 0 && axis != 1) || !qIsFinite(fraction) || !qIsFinite(steps) || steps == 0) return;
    const double factor = qPow(.85, qBound(-20.0, steps, 20.0));
    double xmin = xMinimum(), xmax = xMaximum(), ymin = yMinimum(), ymax = yMaximum();
    if (axis == 0) {
        const double anchor = xmin + 0.5 * (xmax - xmin);
        xmin = anchor + (xmin - anchor) * factor;
        xmax = anchor + (xmax - anchor) * factor;
    } else {
        const double anchor = ymin + 0.5 * (ymax - ymin);
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

double PlotItem::nearestRawX(double x) const
{
    const auto nearest = PlotSeriesStore::nearestX(m_seriesSnapshot, x);
    return nearest.has_value() ? *nearest : x;
}

void PlotItem::rebuildTicksLocked()
{
    auto makeTicks = [](double lo, double hi) {
        QVariantList ticks;
        if (!(hi > lo) || !qIsFinite(lo) || !qIsFinite(hi)) return ticks;
        const double raw = (hi - lo) / 7.0;
        const double magnitude = qPow(10.0, qFloor(qLn(raw) / qLn(10.0)));
        const double normalized = raw / magnitude;
        const double step = (normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0) * magnitude;
        const qint64 first = static_cast<qint64>(qCeil(lo / step - 1e-12));
        const qint64 last = static_cast<qint64>(qFloor(hi / step + 1e-12));
        for (qint64 i = first; i <= last && ticks.size() < 12; ++i) {
            const double value = i * step;
            ticks.append(QVariantMap{{QStringLiteral("value"), value},
                                     {QStringLiteral("label"), QString::number(value, 'g', 12)}});
        }
        if (ticks.isEmpty()) {
            ticks.append(QVariantMap{{QStringLiteral("value"), lo},
                                     {QStringLiteral("label"), QString::number(lo, 'g', 12)}});
            ticks.append(QVariantMap{{QStringLiteral("value"), hi},
                                     {QStringLiteral("label"), QString::number(hi, 'g', 12)}});
        }
        return ticks;
    };
    m_xTicks = makeTicks(m_xMinimum, m_xMaximum);
    m_yTicks = makeTicks(m_yMinimum, m_yMaximum);
}

void PlotItem::updateCursorValuesLocked()
{
    m_cursorReadouts.clear();
    if (m_cursorMode == NoCursor) return;
    m_cursorValues.resize(m_seriesSnapshot.series.size());
    const double keys[] = {m_cursorX1, m_cursorX2};
    QHash<PlotSeriesId, PlotSample> samplesByCursor[2];
    for (int c = 0; c < (m_cursorMode == DoubleCursor ? 2 : 1); ++c)
        if (keys[c] >= m_xMinimum && keys[c] <= m_xMaximum)
        for (const auto &sample : PlotSeriesStore::nearestSamples(m_seriesSnapshot, keys[c]))
            samplesByCursor[c].insert(sample.id, sample);
    for (int s = 0; s < m_seriesSnapshot.series.size(); ++s) {
        m_cursorValues[s].resize(m_cursorMode == DoubleCursor ? 2 : 1);
        for (int c = 0; c < m_cursorValues[s].size(); ++c) {
            double value = qQNaN();
            QColor color;
            const auto sample = samplesByCursor[c].constFind(m_seriesSnapshot.series.at(s)->id);
            if (sample != samplesByCursor[c].cend()) {
                value = sample->y;
                color = sample->color;
            }
            m_cursorValues[s][c] = value;
            if (qIsFinite(value)) {
                QString rawText = QString::number(value, 'f', 12);
                while (rawText.contains(QLatin1Char('.')) && rawText.endsWith(QLatin1Char('0')))
                    rawText.chop(1);
                if (rawText.endsWith(QLatin1Char('.'))) rawText.chop(1);
                m_cursorReadouts.append(QVariantMap{{QStringLiteral("x"), keys[c]},
                                                    {QStringLiteral("y"), value},
                                                    {QStringLiteral("displayY"), normalizedYLocked(m_seriesSnapshot.series.at(s)->id, value)},
                                                    {QStringLiteral("seriesId"), m_seriesSnapshot.series.at(s)->id},
                                                    {QStringLiteral("cursorIndex"), c + 1},
                                                    {QStringLiteral("sampleX"), sample->x},
                                                    {QStringLiteral("text"), QString::number(value, 'g', 6)},
                                                    {QStringLiteral("rawText"), rawText},
                                                    {QStringLiteral("color"), color}});
            }
        }
    }
}

bool PlotItem::cursorHit(double pixelX, int *cursorIndex) const
{
    if (m_cursorMode == NoCursor || width() <= 0) return false;
    const double scale = width() / qMax(m_xMaximum - m_xMinimum, 1e-12);
    const double tolerance = 8.0;
    const double d1 = qAbs((m_cursorX1 - m_xMinimum) * scale - pixelX);
    const double d2 = qAbs((m_cursorX2 - m_xMinimum) * scale - pixelX);
    if (d1 <= tolerance && (m_cursorMode != DoubleCursor || d1 <= d2)) { if (cursorIndex) *cursorIndex = 1; return true; }
    if (m_cursorMode == DoubleCursor && d2 <= tolerance) { if (cursorIndex) *cursorIndex = 2; return true; }
    return false;
}
QSGNode *PlotItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QMutexLocker lock(&m_dataMutex);
    auto *root = oldNode ? static_cast<PlotRoot *>(oldNode) : new PlotRoot;

    const double xs = qMax(m_xMaximum - m_xMinimum, 1e-12);
    const QVector<double> curveView{m_xMinimum, m_xMaximum, m_yMinimum, m_yMaximum,
                                    width(), height(), m_lineWidth, double(m_highlightedSeries),
                                    m_normalizeY ? 1.0 : 0.0};
    if (root->curveResult != m_lodResult || root->curveView != curveView) {
        const LodResult empty;
        LodResult lod = m_lodResult ? *m_lodResult : empty;
        if (m_normalizeY) {
            // Rescale in place on the GUI copy; the shared LOD stays raw.
            for (LodSegment &segment : lod.segments) {
                const auto it = m_normalization.constFind(segment.seriesId);
                if (it == m_normalization.cend()) continue;
                for (QPointF &point : segment.points)
                    if (qIsFinite(point.y()))
                        point.setY(it->second > 0.0 ? (point.y() - it->first) / it->second : 0.5);
            }
        }
        if (m_highlightedSeries >= 0) {
            // Emphasize in geometry only: no data copy or new LOD job.
            QVector<LodSegment> selected;
            QVector<LodSegment> remaining;
            for (auto segment : std::as_const(lod.segments)) {
                if (segment.seriesId == m_highlightedSeries) {
                    segment.lineWidth = (segment.lineWidth > 0 ? segment.lineWidth : m_lineWidth) + 2;
                    selected.append(std::move(segment));
                } else remaining.append(std::move(segment));
            }
            remaining.append(selected);
            lod.segments = std::move(remaining);
        }
        const GeometryRequest geometryRequest{
            {m_xMinimum, m_xMaximum, m_yMinimum, m_yMaximum, width(), height()},
            m_lineWidth};
        const GeometryResult geometry = PlotGeometryBuilder::build(
            lod, geometryRequest);

        while (root->lineNodes.size() < geometry.segments.size())
            createLineNode(root);
        for (int i = 0; i < root->lineNodes.size(); ++i) {
            auto *node = root->lineNodes.at(i);
            if (i < geometry.segments.size()) {
                uploadLineNode(node, geometry.segments.at(i));
            } else if (node->geometry()) {
                node->geometry()->allocate(0);
                node->geometry()->markVertexDataDirty();
                node->markDirty(QSGNode::DirtyGeometry);
            }
        }
        root->curveResult = m_lodResult;
        root->curveView = curveView;
    }
    if (m_cursorMode != NoCursor && m_xMaximum > m_xMinimum) {
        const double x = (m_cursorX1 - m_xMinimum) / xs * width();
        if (!root->cursorNode) {
            root->cursorNode = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
            geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->cursorNode->setGeometry(geometry);
            root->cursorNode->setFlag(QSGNode::OwnsGeometry);
            auto *material = new QSGFlatColorMaterial;
            material->setColor(QColor("#e34d59"));
            root->cursorNode->setMaterial(material);
            root->cursorNode->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(root->cursorNode);
        }
        auto *geometry = root->cursorNode->geometry();
        geometry->allocate(4);
        auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
        vertices[0].set(x - 0.75, 0); vertices[1].set(x + 0.75, 0);
        vertices[2].set(x - 0.75, height()); vertices[3].set(x + 0.75, height());
        geometry->markVertexDataDirty();
        root->cursorNode->markDirty(QSGNode::DirtyGeometry);
        if (m_cursorMode == DoubleCursor) {
            if (!root->cursorNode2) {
                auto *second = new QSGGeometryNode;
                auto *g2 = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
                g2->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                second->setGeometry(g2);
                second->setFlag(QSGNode::OwnsGeometry);
                auto *m2 = new QSGFlatColorMaterial;
                m2->setColor(QColor("#4e79e7"));
                second->setMaterial(m2);
                second->setFlag(QSGNode::OwnsMaterial);
                root->appendChildNode(second);
                root->cursorNode2 = second;
            }
            auto *second = root->cursorNode2;
            const double x2 = (m_cursorX2 - m_xMinimum) / xs * width();
            auto *g2 = second->geometry();
            g2->allocate(4);
            auto *v2 = static_cast<QSGGeometry::Point2D *>(g2->vertexData());
            v2[0].set(x2 - .75, 0);
            v2[1].set(x2 + .75, 0);
            v2[2].set(x2 - .75, height());
            v2[3].set(x2 + .75, height());
            g2->markVertexDataDirty();
            second->markDirty(QSGNode::DirtyGeometry);
        } else if (root->cursorNode2 && root->cursorNode2->geometry()) {
            root->cursorNode2->geometry()->allocate(0);
            root->cursorNode2->geometry()->markVertexDataDirty();
            root->cursorNode2->markDirty(QSGNode::DirtyGeometry);
        }
    } else if (root->cursorNode && root->cursorNode->geometry()) {
        root->cursorNode->geometry()->allocate(0);
        root->cursorNode->geometry()->markVertexDataDirty();
        root->cursorNode->markDirty(QSGNode::DirtyGeometry);
        if (root->cursorNode2 && root->cursorNode2->geometry()) {
            root->cursorNode2->geometry()->allocate(0);
            root->cursorNode2->geometry()->markVertexDataDirty();
            root->cursorNode2->markDirty(QSGNode::DirtyGeometry);
        }
    }
    return root;
}
void PlotItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    requestLod();
    update();
}

void PlotItem::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton) return;
    emit activated();
    int cursorIndex = 0;
    {
        QMutexLocker lock(&m_dataMutex);
        if (cursorHit(e->position().x(), &cursorIndex)) {
            m_cursorDragIndex = cursorIndex;
            m_dragging = false;
            e->accept();
            return;
        }
    }
    m_cursorDragIndex = 0;
    m_dragging = true;
    m_dragStartPixel = e->position();
    m_dragStartXMinimum = m_xMinimum;
    m_dragStartXMaximum = m_xMaximum;
    m_dragStartYMinimum = m_yMinimum;
    m_dragStartYMaximum = m_yMaximum;
    e->accept();
}

void PlotItem::mouseMoveEvent(QMouseEvent *e)
{
    if (m_cursorDragIndex) {
        const double x = pixelToData(e->position()).x();
        if (m_cursorMode == DoubleCursor) {
            // Cursors never cross: dragging one past the other hands the drag to the other cursor.
            double x1, x2;
            { QMutexLocker lock(&m_dataMutex); x1 = m_cursorX1; x2 = m_cursorX2; }
            if (m_cursorDragIndex == 1 && x > x2) { setCursorX(x2, 1); m_cursorDragIndex = 2; }
            else if (m_cursorDragIndex == 2 && x < x1) { setCursorX(x1, 2); m_cursorDragIndex = 1; }
        }
        setCursorX(x, m_cursorDragIndex);
        e->accept();
        return;
    }
    if (!m_dragging) return;
    QPointF d = e->position() - m_dragStartPixel;
    double xs = m_dragStartXMaximum - m_dragStartXMinimum, ys = m_dragStartYMaximum - m_dragStartYMinimum;
    setRange(m_dragStartXMinimum - d.x() / qMax(width(), 1.) * xs, m_dragStartXMaximum - d.x() / qMax(width(), 1.) * xs,
             m_dragStartYMinimum + d.y() / qMax(height(), 1.) * ys, m_dragStartYMaximum + d.y() / qMax(height(), 1.) * ys);
    e->accept();
}

void PlotItem::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    m_cursorDragIndex = 0;
    e->accept();
}

void PlotItem::hoverMoveEvent(QHoverEvent *e)
{
    Q_UNUSED(e);
}

void PlotItem::wheelEvent(QWheelEvent *e)
{
    // Zoom both axes around the pointer position.
    const double factor = e->angleDelta().y() > 0 ? .85 : 1 / .85;
    const QPointF anchor = pixelToData(e->position());
    setRange(anchor.x() - (anchor.x() - m_xMinimum) * factor,
             anchor.x() + (m_xMaximum - anchor.x()) * factor,
             anchor.y() - (anchor.y() - m_yMinimum) * factor,
             anchor.y() + (m_yMaximum - anchor.y()) * factor);
    e->accept();
}

void PlotItem::setRange(double xmin,double xmax,double ymin,double ymax)
{
    if (!qIsFinite(xmin) || !qIsFinite(xmax) || !qIsFinite(ymin) || !qIsFinite(ymax)
        || xmax <= xmin || ymax <= ymin)
        return;
    {
        QMutexLocker lock(&m_dataMutex);
        if (m_xMinimum == xmin && m_xMaximum == xmax && m_yMinimum == ymin && m_yMaximum == ymax) return;
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
QPointF PlotItem::pixelToData(const QPointF &p) const
{
    QMutexLocker lock(&m_dataMutex);
    return {m_xMinimum + p.x() / qMax(width(), 1.) * (m_xMaximum - m_xMinimum),
            m_yMaximum - p.y() / qMax(height(), 1.) * (m_yMaximum - m_yMinimum)};
}
