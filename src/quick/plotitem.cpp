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
                           double itemHeight, double hx, double hy)
{
    auto *geometry = node->geometry();
    const int vertexCount = segment.size() * 2;
    geometry->allocate(vertexCount);
    auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
    for (int i = 0; i < segment.size(); ++i) {
        const QPointF p = segment.at(i);
        const QPointF a = i ? segment.at(i - 1) : p;
        const QPointF b = i + 1 < segment.size() ? segment.at(i + 1) : p;
        QPointF tangent = b - a;
        double length = qSqrt(tangent.x() * tangent.x() + tangent.y() * tangent.y());
        if (length < 1e-12) length = 1.0;
        const QPointF normal(-tangent.y() / length * hx, tangent.x() / length * hy);
        const double x = (p.x() - xmin) / xs * itemWidth;
        const double y = itemHeight - (p.y() - ymin) / ys * itemHeight;
        vertices[i * 2].set(x + normal.x(), y - normal.y());
        vertices[i * 2 + 1].set(x - normal.x(), y + normal.y());
    }
    geometry->markVertexDataDirty();
    if (auto *material = static_cast<QSGFlatColorMaterial *>(node->material()))
        material->setColor(color);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

} // namespace

PlotItem::PlotItem(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton); }
void PlotItem::setLineWidth(double width) { const double v = qBound(1.0, width, 12.0); QMutexLocker lock(&m_dataMutex); if (qFuzzyCompare(v, m_lineWidth)) return; m_lineWidth = v; emit lineWidthChanged(); update(); }
void PlotItem::setCursorEnabled(bool enabled) { QMutexLocker lock(&m_dataMutex); if (m_cursorEnabled == enabled) return; m_cursorEnabled = enabled; emit cursorChanged(); update(); }
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
    update();
}
void PlotItem::clearSeries() { { QMutexLocker lock(&m_dataMutex); m_series.clear(); } setRange(0, 1, -1, 1); }
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
QVector<QPointF> PlotItem::buildLod(const Series &series) const
{
    QVector<QPointF> out;
    const QVector<QPointF> &points = series.points;
    if (points.isEmpty() || width() <= 1 || m_xMaximum <= m_xMinimum)
        return out;

    const int buckets = qBound(64, qCeil(width() * 1.5), 8192);
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
        if (point.x() < m_xMinimum || point.x() > m_xMaximum)
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
    const double hx = m_lineWidth * .5 / qMax(width(), 1.) * xs;
    const double hy = m_lineWidth * .5 / qMax(height(), 1.) * ys;
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
                           width(), height(), hx, hy);
        } else if (node->geometry()) {
            node->geometry()->allocate(0);
            node->geometry()->markVertexDataDirty();
            node->markDirty(QSGNode::DirtyGeometry);
        }
    }
    if (m_cursorEnabled && m_xMaximum > m_xMinimum) {
        const double x = (m_cursorX - m_xMinimum) / xs * width();
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
    } else if (root->cursorNode && root->cursorNode->geometry()) {
        root->cursorNode->geometry()->allocate(0);
        root->cursorNode->geometry()->markVertexDataDirty();
        root->cursorNode->markDirty(QSGNode::DirtyGeometry);
    }
    return root;
}
void PlotItem::geometryChange(const QRectF &n,const QRectF &o){QQuickItem::geometryChange(n,o);update();}
void PlotItem::mousePressEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton&&e->button()!=Qt::MiddleButton)return;m_dragging=true;m_dragStartPixel=e->position();m_dragStartXMinimum=m_xMinimum;m_dragStartXMaximum=m_xMaximum;m_dragStartYMinimum=m_yMinimum;m_dragStartYMaximum=m_yMaximum;e->accept();}
void PlotItem::mouseMoveEvent(QMouseEvent *e){if(!m_dragging)return;QPointF d=e->position()-m_dragStartPixel;double xs=m_dragStartXMaximum-m_dragStartXMinimum,ys=m_dragStartYMaximum-m_dragStartYMinimum;setRange(m_dragStartXMinimum-d.x()/qMax(width(),1.)*xs,m_dragStartXMaximum-d.x()/qMax(width(),1.)*xs,m_dragStartYMinimum+d.y()/qMax(height(),1.)*ys,m_dragStartYMaximum+d.y()/qMax(height(),1.)*ys);e->accept();}
void PlotItem::mouseReleaseEvent(QMouseEvent *e){m_dragging=false;e->accept();}
void PlotItem::hoverMoveEvent(QHoverEvent *e){if(!m_cursorEnabled)return;const QPointF p=e->position();m_cursorX=pixelToData(p).x();emit cursorChanged();update();}
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
    }
    emit viewChanged();
    emit rangeChanged(xmin, xmax, ymin, ymax);
    update();
}
QPointF PlotItem::pixelToData(const QPointF &p) const
{
    QMutexLocker lock(&m_dataMutex);
    return {m_xMinimum + p.x() / qMax(width(), 1.) * (m_xMaximum - m_xMinimum),
            m_yMaximum - p.y() / qMax(height(), 1.) * (m_yMaximum - m_yMinimum)};
}
