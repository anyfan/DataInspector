#include "plotitem.h"
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
    root->appendChildNode(node);
    root->lineNodes.append(node);
    return node;
}

static void updateLineNode(QSGGeometryNode *node, const QVector<QPointF> &segment,
                           const QColor &color, double xmin, double xs,
                           double ymin, double ys, double itemWidth,
                           double itemHeight, double lineWidth)
{
    auto *geometry = node->geometry();
    const int vertexCount = segment.size() * 2;
    geometry->allocate(vertexCount);
    auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
    for (int i = 0; i < segment.size(); ++i) {
        const QPointF p = segment.at(i);
        const QPointF a = i ? segment.at(i - 1) : p;
        const QPointF b = i + 1 < segment.size() ? segment.at(i + 1) : p;
        const QPointF projectedA((a.x() - xmin) / xs * itemWidth, itemHeight - (a.y() - ymin) / ys * itemHeight);
        const QPointF projectedB((b.x() - xmin) / xs * itemWidth, itemHeight - (b.y() - ymin) / ys * itemHeight);
        QPointF tangent = projectedB - projectedA;
        double length = qSqrt(tangent.x() * tangent.x() + tangent.y() * tangent.y());
        if (length < 1e-12) length = 1.0;
        const QPointF normal(-tangent.y() / length * lineWidth * .5, tangent.x() / length * lineWidth * .5);
        const double x = (p.x() - xmin) / xs * itemWidth;
        const double y = itemHeight - (p.y() - ymin) / ys * itemHeight;
        const double x0 = qBound(0.0, x + normal.x(), itemWidth);
        const double y0 = qBound(0.0, y - normal.y(), itemHeight);
        const double x1 = qBound(0.0, x - normal.x(), itemWidth);
        const double y1 = qBound(0.0, y + normal.y(), itemHeight);
        vertices[i * 2].set(x0, y0);
        vertices[i * 2 + 1].set(x1, y1);
    }
    geometry->markVertexDataDirty();
    if (auto *material = static_cast<QSGFlatColorMaterial *>(node->material()))
        material->setColor(color);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

} // namespace

PlotItem::PlotItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setClip(true);
    rebuildTicksLocked();
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
            m_cursorX1 = m_xMinimum + span * .05;
            m_cursorX2 = m_xMinimum + span * .95;
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
        if (cursorIndex == 2) m_cursorX2 = clamped;
        else { m_cursorX1 = clamped; m_cursorX = clamped; }
        m_cursorX = cursorIndex == 2 ? m_cursorX2 : m_cursorX1;
        updateCursorValuesLocked();
    }
    emit cursorChanged(); emit cursorDeltaTChanged(); emit cursorValuesChanged(); update();
}
void PlotItem::setSeries(const QVector<double> &time, const QVector<double> &values)
{
    clearSeries();
    appendSeries(time, values, QColor("#4ea1ff"));
}
void PlotItem::appendSeries(const QVector<double> &time, const QVector<double> &values, const QColor &color)
{
    QMutexLocker lock(&m_dataMutex);
    Series series; const int n = qMin(time.size(), values.size()); series.points.reserve(n); series.color = color.isValid() ? color : QColor("#4ea1ff");
    double previousTime = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        const double timestamp = time.at(i);
        const double value = values.at(i);
        if (!qIsFinite(timestamp)) {
            series.monotonicTime = false;
            series.points.append(QPointF(qQNaN(), qQNaN()));
            continue;
        }
        if (timestamp < previousTime)
            series.monotonicTime = false;
        previousTime = timestamp;
        series.points.append(QPointF(timestamp, qIsFinite(value) ? value : qQNaN()));
    }
    m_series.append(std::move(series));
    m_cursorValues.resize(m_series.size());
    updateCursorValuesLocked();
    update();
}
void PlotItem::clearSeries() { { QMutexLocker lock(&m_dataMutex); m_series.clear(); m_cursorValues.clear(); } setRange(0, 1, -1, 1); }
void PlotItem::fitView()
{
    double xmin = std::numeric_limits<double>::max(), xmax = std::numeric_limits<double>::lowest(), ymin = xmin, ymax = xmax;
    {
        QMutexLocker lock(&m_dataMutex);
        for (const Series &series : std::as_const(m_series))
            for (const QPointF &p : series.points)
                if (qIsFinite(p.x()) && qIsFinite(p.y())) { xmin = qMin(xmin, p.x()); xmax = qMax(xmax, p.x()); ymin = qMin(ymin, p.y()); ymax = qMax(ymax, p.y()); }
    }
    if (xmin == std::numeric_limits<double>::max()) { setRange(0, 1, -1, 1); return; }
    if (qFuzzyCompare(xmin, xmax)) { xmin -= .5; xmax += .5; } else { const double p = qMax((xmax-xmin)*.02, 1e-9); xmin -= p; xmax += p; }
    if (qFuzzyCompare(ymin, ymax)) { ymin -= .5; ymax += .5; } else { const double p = qMax((ymax-ymin)*.08, 1e-9); ymin -= p; ymax += p; }
    setRange(xmin, xmax, ymin, ymax);
}
void PlotItem::fitY()
{
    double ymin = std::numeric_limits<double>::max();
    double ymax = std::numeric_limits<double>::lowest();
    {
        QMutexLocker lock(&m_dataMutex);
        for (const Series &series : std::as_const(m_series)) {
            for (const QPointF &p : series.points) {
                if (!qIsFinite(p.x()) || !qIsFinite(p.y())) continue;
                ymin = qMin(ymin, p.y());
                ymax = qMax(ymax, p.y());
            }
        }
    }
    double xmin, xmax;
    {
        QMutexLocker lock(&m_dataMutex);
        xmin = m_xMinimum;
        xmax = m_xMaximum;
    }
    if (ymin == std::numeric_limits<double>::max()) {
        setRange(xmin, xmax, -1.0, 1.0);
        return;
    }
    if (qFuzzyCompare(ymin, ymax)) {
        ymin -= .5;
        ymax += .5;
    } else {
        const double padding = qMax((ymax - ymin) * .08, 1e-9);
        ymin -= padding;
        ymax += padding;
    }
    setRange(xmin, xmax, ymin, ymax);
}
void PlotItem::setXRange(double xmin, double xmax)
{
    double ymin, ymax;
    { QMutexLocker lock(&m_dataMutex); ymin = m_yMinimum; ymax = m_yMaximum; }
    setRange(xmin, xmax, ymin, ymax);
}

double PlotItem::nearestRawX(double x) const
{
    double nearest = x;
    double distance = std::numeric_limits<double>::max();
    for (const Series &series : std::as_const(m_series)) {
        for (const QPointF &point : series.points) {
            if (!qIsFinite(point.x())) continue;
            const double d = qAbs(point.x() - x);
            if (d < distance) { distance = d; nearest = point.x(); }
        }
    }
    return distance == std::numeric_limits<double>::max() ? x : nearest;
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
    m_cursorValues.resize(m_series.size());
    const double keys[] = {m_cursorX1, m_cursorX2};
    for (int s = 0; s < m_series.size(); ++s) {
        m_cursorValues[s].resize(m_cursorMode == DoubleCursor ? 2 : 1);
        for (int c = 0; c < m_cursorValues[s].size(); ++c) {
            double value = qQNaN(), best = std::numeric_limits<double>::max();
            for (const QPointF &point : m_series.at(s).points) {
                if (!qIsFinite(point.x()) || !qIsFinite(point.y())) continue;
                const double d = qAbs(point.x() - keys[c]);
                if (d < best) { best = d; value = point.y(); }
            }
            m_cursorValues[s][c] = value;
            if (qIsFinite(value)) {
                QString rawText = QString::number(value, 'f', 12);
                while (rawText.contains(QLatin1Char('.')) && rawText.endsWith(QLatin1Char('0')))
                    rawText.chop(1);
                if (rawText.endsWith(QLatin1Char('.'))) rawText.chop(1);
                m_cursorReadouts.append(QVariantMap{{QStringLiteral("x"), keys[c]},
                                                    {QStringLiteral("y"), value},
                                                    {QStringLiteral("text"), QString::number(value, 'g', 8)},
                                                    {QStringLiteral("rawText"), rawText},
                                                    {QStringLiteral("color"), m_series.at(s).color}});
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
QVector<QPointF> PlotItem::buildLod(const Series &series) const
{
    QVector<QPointF> out;
    const QVector<QPointF> &points = series.points;
    if (points.isEmpty() || width() <= 1 || m_xMaximum <= m_xMinimum)
        return out;

    // Keep the vertex budget proportional to pixels. QCustomPlot's adaptive
    // sampling follows the same principle and avoids pushing millions of raw
    // points through the scene graph on every repaint.
    const int buckets = qBound(64, qCeil(width()), 4096);
    if (series.cachedBuckets == buckets
        && series.cachedXMinimum == m_xMinimum
        && series.cachedXMaximum == m_xMaximum)
        return series.cachedLod;

    const double span = m_xMaximum - m_xMinimum;
    const double bucketWidth = span / buckets;
    out.reserve(buckets * 2);

    auto first = points.cbegin();
    auto last = points.cend();
    if (series.monotonicTime) {
        first = std::lower_bound(points.cbegin(), points.cend(), m_xMinimum,
                                 [](const QPointF &point, double x) { return point.x() < x; });
        last = std::upper_bound(first, points.cend(), m_xMaximum,
                                [](double x, const QPointF &point) { return x < point.x(); });
        // Keep one neighbour on each side so a sparse line still reaches the
        // viewport boundary instead of visibly popping during a pan.
        if (first != points.cbegin()) --first;
        if (last != points.cend()) ++last;
    }

    int currentBucket = -1;
    QPointF minimum;
    QPointF maximum;
    bool hasPoint = false;
    auto flush = [&]() {
        if (!hasPoint)
            return;
        if (minimum.x() <= maximum.x()) {
            out.append(minimum);
            if (minimum != maximum) out.append(maximum);
        } else {
            out.append(maximum);
            if (minimum != maximum) out.append(minimum);
        }
        hasPoint = false;
    };

    for (auto it = first; it != last; ++it) {
        const QPointF &point = *it;
        if (!qIsFinite(point.x()) || !qIsFinite(point.y())) {
            flush();
            currentBucket = -1;
            if (out.isEmpty() || !qIsNaN(out.constLast().x()))
                out.append(QPointF(qQNaN(), qQNaN()));
            continue;
        }
        // The monotonic fast path deliberately includes one point on each
        // side of the viewport. Keep those points so a line crossing a narrow
        // view still has two vertices after LOD reduction.
        if (!series.monotonicTime
            && (point.x() < m_xMinimum || point.x() > m_xMaximum))
            continue;
        const int bucket = qBound(0, static_cast<int>((point.x() - m_xMinimum) / bucketWidth), buckets - 1);
        if (bucket != currentBucket) {
            flush();
            currentBucket = bucket;
        }
        if (!hasPoint) {
            minimum = maximum = point;
            hasPoint = true;
        } else {
            if (point.y() < minimum.y()) minimum = point;
            if (point.y() > maximum.y()) maximum = point;
        }
    }
    flush();
    while (!out.isEmpty() && qIsNaN(out.constLast().x()))
        out.removeLast();
    series.cachedXMinimum = m_xMinimum;
    series.cachedXMaximum = m_xMaximum;
    series.cachedBuckets = buckets;
    series.cachedLod = out;
    return out;
}
QSGNode *PlotItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QMutexLocker lock(&m_dataMutex);
    auto *root = oldNode ? static_cast<PlotRoot *>(oldNode) : new PlotRoot;

    const double xs = qMax(m_xMaximum - m_xMinimum, 1e-12);
    const double ys = qMax(m_yMaximum - m_yMinimum, 1e-12);
    QVector<QVector<QPointF>> segments;
    QVector<QColor> segmentColors;
    auto collectSeries = [&](const Series &series) {
        const QVector<QPointF> lod = buildLod(series);
        QVector<QPointF> segment;
        auto addSegment = [&]() {
            if (segment.size() >= 2) {
                segments.append(segment);
                segmentColors.append(series.color);
            }
            segment.clear();
        };
        for (const QPointF &point : lod) {
            if (qIsFinite(point.x()) && qIsFinite(point.y())) segment.append(point);
            else addSegment();
        }
        addSegment();
    };
    for (const Series &series : std::as_const(m_series)) collectSeries(series);

    while (root->lineNodes.size() < segments.size())
        createLineNode(root);
    for (int i = 0; i < root->lineNodes.size(); ++i) {
        auto *node = root->lineNodes.at(i);
        if (i < segments.size()) {
            updateLineNode(node, segments.at(i), segmentColors.at(i),
                           m_xMinimum, xs, m_yMinimum, ys,
                           width(), height(), m_lineWidth);
        } else if (node->geometry()) {
            node->geometry()->allocate(0);
            node->geometry()->markVertexDataDirty();
            node->markDirty(QSGNode::DirtyGeometry);
        }
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
                g2->setDrawingMode(QSGGeometry::DrawTriangleStrip); second->setGeometry(g2); second->setFlag(QSGNode::OwnsGeometry);
                auto *m2 = new QSGFlatColorMaterial; m2->setColor(QColor("#4e79e7")); second->setMaterial(m2); second->setFlag(QSGNode::OwnsMaterial); root->appendChildNode(second); root->cursorNode2 = second;
            }
            auto *second = root->cursorNode2;
            const double x2 = (m_cursorX2 - m_xMinimum) / xs * width();
            auto *g2 = second->geometry(); g2->allocate(4); auto *v2 = static_cast<QSGGeometry::Point2D *>(g2->vertexData());
            v2[0].set(x2 - .75, 0); v2[1].set(x2 + .75, 0); v2[2].set(x2 - .75, height()); v2[3].set(x2 + .75, height()); g2->markVertexDataDirty(); second->markDirty(QSGNode::DirtyGeometry);
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
void PlotItem::geometryChange(const QRectF &n,const QRectF &o){QQuickItem::geometryChange(n,o);update();}
void PlotItem::mousePressEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton&&e->button()!=Qt::MiddleButton)return;int cursorIndex=0;{QMutexLocker lock(&m_dataMutex);if(cursorHit(e->position().x(),&cursorIndex)){m_cursorDragIndex=cursorIndex;m_dragging=false;e->accept();return;}}m_cursorDragIndex=0;m_dragging=true;m_dragStartPixel=e->position();m_dragStartXMinimum=m_xMinimum;m_dragStartXMaximum=m_xMaximum;m_dragStartYMinimum=m_yMinimum;m_dragStartYMaximum=m_yMaximum;e->accept();}
void PlotItem::mouseMoveEvent(QMouseEvent *e){if(m_cursorDragIndex){setCursorX(pixelToData(e->position()).x(),m_cursorDragIndex);e->accept();return;}if(!m_dragging)return;QPointF d=e->position()-m_dragStartPixel;double xs=m_dragStartXMaximum-m_dragStartXMinimum,ys=m_dragStartYMaximum-m_dragStartYMinimum;setRange(m_dragStartXMinimum-d.x()/qMax(width(),1.)*xs,m_dragStartXMaximum-d.x()/qMax(width(),1.)*xs,m_dragStartYMinimum+d.y()/qMax(height(),1.)*ys,m_dragStartYMaximum+d.y()/qMax(height(),1.)*ys);e->accept();}
void PlotItem::mouseReleaseEvent(QMouseEvent *e){m_dragging=false;m_cursorDragIndex=0;e->accept();}
void PlotItem::hoverMoveEvent(QHoverEvent *e){Q_UNUSED(e);}
void PlotItem::wheelEvent(QWheelEvent *e){const double f=e->angleDelta().y()>0?.85:1/.85;const QPointF a=pixelToData(e->position());setRange(a.x()-(a.x()-m_xMinimum)*f,a.x()+(m_xMaximum-a.x())*f,a.y()-(a.y()-m_yMinimum)*f,a.y()+(m_yMaximum-a.y())*f);e->accept();}
void PlotItem::setRange(double xmin,double xmax,double ymin,double ymax)
{
    if (xmax <= xmin || ymax <= ymin)
        return;
    {
        QMutexLocker lock(&m_dataMutex);
        m_xMinimum = xmin;
        m_xMaximum = xmax;
        m_yMinimum = ymin;
        m_yMaximum = ymax;
        rebuildTicksLocked();
    }
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
