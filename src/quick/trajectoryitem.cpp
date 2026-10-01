#include "trajectoryitem.h"
#include <QMutexLocker>
#include <QThreadPool>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QtMath>

namespace {
QThreadPool &trajectoryPool()
{
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return pool;
}
struct TrajectoryRoot : QSGNode {
    QVector<QSGGeometryNode *> nodes;
    std::shared_ptr<const TrajectoryPreview> preview;
};
}
TrajectoryItem::TrajectoryItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setClip(true);
    m_poll.setInterval(12);
    connect(&m_poll, &QTimer::timeout, this, &TrajectoryItem::finish);
}
TrajectoryItem::~TrajectoryItem() { if (m_job) m_job->cancelled = true; }
QString TrajectoryItem::error() const
{
    QMutexLocker lock(&m_mutex);
    return m_data ? m_data->error : QStringLiteral("请选择至少两个坐标信号");
}
bool TrajectoryItem::planar() const
{
    QMutexLocker lock(&m_mutex);
    return m_data ? m_data->planar : int(bool(m_axes[0])) + int(bool(m_axes[1])) + int(bool(m_axes[2])) == 2;
}
QVariantList TrajectoryItem::axisLabels() const
{
    QMutexLocker lock(&m_mutex);
    return m_preview ? m_preview->labels : QVariantList{};
}
TrajectoryCamera TrajectoryItem::camera() const { QMutexLocker lock(&m_mutex); return m_camera; }
QRectF TrajectoryItem::orientationRect() const {
    QMutexLocker lock(&m_mutex); return m_preview ? m_preview->orientationRect : QRectF();
}
double TrajectoryItem::lineWidth() const { QMutexLocker lock(&m_mutex); return m_lineWidth; }
QColor TrajectoryItem::axisColor() const { QMutexLocker lock(&m_mutex); return m_axisColor; }
void TrajectoryItem::setLineWidth(double width)
{
    if (!qIsFinite(width)) return;
    width = qBound(1.0, width, 12.0);
    { QMutexLocker lock(&m_mutex); if (m_lineWidth == width) return; m_lineWidth = width; }
    emit styleChanged(); requestPreview();
}
void TrajectoryItem::setAxisColor(const QColor &color)
{
    if (!color.isValid()) return;
    { QMutexLocker lock(&m_mutex); if (m_axisColor == color) return; m_axisColor = color; }
    emit styleChanged(); requestPreview();
}
void TrajectoryItem::setAxes(const std::array<PlotSeriesDataPtr, 3> &axes, bool geographic)
{
    if (m_axes == axes && m_geographic == geographic) return;
    m_axes = axes; m_geographic = geographic;
    if (m_job) m_job->cancelled = true;
    // Do not expose a previous source's position or trajectory while a new one builds.
    { QMutexLocker lock(&m_mutex); m_data.reset(); m_preview.reset(); }
    requestPreview(); emit markersChanged(); update();
}
void TrajectoryItem::setCamera(const TrajectoryCamera &view)
{
    if (!view.valid() || view == camera()) return;
    emit cameraAboutToChange();
    { QMutexLocker lock(&m_mutex); m_camera = view; }
    emit cameraChanged(); requestPreview();
}
void TrajectoryItem::fitView()
{
    auto view = camera(); view.zoom = 1; view.panX = view.panY = view.panDepth = view.viewScale = 0; setCamera(view);
}
void TrajectoryItem::presetView(int preset)
{
    if (preset < 0 || preset > 3) return;
    if (planar()) { fitView(); return; }
    TrajectoryCamera view;
    if (preset == 1) { view.azimuth = 0; view.elevation = 90; }
    if (preset == 2) { view.azimuth = 0; view.elevation = 0; }
    if (preset == 3) { view.azimuth = 90; view.elevation = 0; }
    setCamera(view);
}
void TrajectoryItem::requestPreview()
{
    ++m_revision;
    // Let a camera-only request complete and display it, then render the latest
    // camera. Repeated cancellation starves visible frames during a long drag.
    if (!m_job) start();
    emit previewChanged();
}
void TrajectoryItem::start()
{
    const int selected = int(bool(m_axes[0])) + int(bool(m_axes[1])) + int(bool(m_axes[2]));
    if (selected < 2 || width() <= 0 || height() <= 0) {
        m_poll.stop();
        { QMutexLocker lock(&m_mutex); m_data.reset(); m_preview.reset(); }
        emit previewChanged(); emit markersChanged(); update(); return;
    }
    m_job = std::make_shared<Job>();
    const auto job = m_job;
    job->revision = m_revision; job->camera = camera(); job->size = QSizeF(width(), height());
    const auto axes = m_axes;
    std::shared_ptr<const TrajectoryData> previous;
    { QMutexLocker lock(&m_mutex); previous = m_data; }
    const auto width = lineWidth(); const auto color = axisColor(); const bool geographic = m_geographic;
    const bool interactive = m_dragging;
    trajectoryPool().start([job, axes, previous, width, color, geographic, interactive] {
        try {
            job->data = previous && previous->axes == axes && previous->geographic == geographic ? previous
                : TrajectoryBuilder::build(axes, &job->cancelled, geographic);
            if (!job->cancelled)
                job->preview = std::make_shared<TrajectoryPreview>(TrajectoryBuilder::preview(
                    *job->data, job->camera, job->size, width, color, &job->cancelled, interactive));
        } catch (...) {
            auto failed = std::make_shared<TrajectoryData>();
            failed->axes = axes; failed->geographic = geographic;
            failed->error = QStringLiteral("轨迹构建失败：资源不足"); job->data = failed;
        }
        job->done.store(true, std::memory_order_release);
    });
    m_poll.start();
}
void TrajectoryItem::finish()
{
    if (!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    const auto job = std::move(m_job);
    m_poll.stop();
    if (!job->cancelled && job->data && job->data->axes == m_axes && job->data->geographic == m_geographic) {
        QMutexLocker lock(&m_mutex);
        m_data = job->data; m_preview = job->preview;
        m_displayCamera = job->camera; m_displaySize = job->size;
    }
    if (job->revision != m_revision) start();
    emit previewChanged(); emit markersChanged(); update();
}
void TrajectoryItem::setTimeCursor(int mode, double t1, double t2, double minimum, double maximum)
{
    if (mode < 0 || mode > 2 || !qIsFinite(t1) || !qIsFinite(t2)) return;
    {
        QMutexLocker lock(&m_mutex);
        if (m_cursorMode == mode && m_t1 == t1 && m_t2 == t2 && m_timeMinimum == minimum && m_timeMaximum == maximum) return;
        m_cursorMode = mode; m_t1 = t1; m_t2 = t2;
        m_timeMinimum = minimum; m_timeMaximum = maximum;
    }
    emit markersChanged();
}
QVariantList TrajectoryItem::markers() const
{
    QMutexLocker lock(&m_mutex);
    QVariantList result;
    if (!m_data || !m_data->valid() || !m_preview) return result;
    const auto append = [&](qsizetype index, const QString &label, const QColor &color) {
        const auto xyz = TrajectoryBuilder::position(*m_data, index);
        const auto spatial = TrajectoryBuilder::spatialPosition(*m_data, index);
        const auto screen = TrajectoryBuilder::project(*m_data, spatial, m_displayCamera, m_displaySize);
        const double time = m_data->axes[m_data->timeAxis]->pointAt(index).x();
        QString details = QStringLiteral("%1  t=%2").arg(label, QString::number(time, 'g', 12));
        const QStringList names = m_data->geographic ? QStringList{QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("高度")}
                                                    : QStringList{"X", "Y", "Z"};
        for (int axis = 0; axis < 3; ++axis) if (m_data->axes[axis]) {
            details += QStringLiteral("  %1=%2").arg(names[axis], QString::number(xyz[axis], 'g', 12));
            if (m_data->geographic) details += axis == 2 ? QStringLiteral(" m") : QStringLiteral("°");
        }
        result.append(QVariantMap{{"x", screen.x()}, {"y", screen.y()}, {"color", color},
            {"text", label}, {"details", details}, {"time", time},
            {"spatialX", spatial[0]}, {"spatialY", spatial[1]}, {"spatialZ", spatial[2]},
            {"rawX", m_data->axes[0] ? QVariant(xyz[0]) : QVariant()},
            {"rawY", m_data->axes[1] ? QVariant(xyz[1]) : QVariant()},
            {"rawZ", m_data->axes[2] ? QVariant(xyz[2]) : QVariant()}});
    };
    append(m_data->runs.first().first, QStringLiteral("起点"), QColor("#1aa05b"));
    append(m_data->runs.last().second, QStringLiteral("终点"), QColor("#e34d59"));
    for (int cursor = 0; cursor < m_cursorMode; ++cursor) {
        const double time = cursor == 0 ? m_t1 : m_t2;
        if (time < m_timeMinimum || time > m_timeMaximum) continue;
        const auto sample = TrajectoryBuilder::nearestSample(*m_data, time);
        if (sample) append(*sample, QStringLiteral("游标 %1").arg(cursor + 1),
                           QColor(cursor == 0 ? "#e34d59" : "#4e79e7"));
    }
    return result;
}
QSGNode *TrajectoryItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *root = oldNode ? static_cast<TrajectoryRoot *>(oldNode) : new TrajectoryRoot;
    std::shared_ptr<const TrajectoryPreview> preview;
    { QMutexLocker lock(&m_mutex); preview = m_preview; }
    if (root->preview == preview) return root;
    const int count = preview ? preview->geometry.segments.size() : 0;
    while (root->nodes.size() < count) {
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGFlatColorMaterial); node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node); root->nodes.append(node);
    }
    for (int i = 0; i < root->nodes.size(); ++i) {
        auto *node = root->nodes.at(i); auto *geometry = node->geometry();
        const GeometrySegment *segment = i < count ? &preview->geometry.segments.at(i) : nullptr;
        geometry->setDrawingMode(segment && segment->triangleList ? QSGGeometry::DrawTriangles : QSGGeometry::DrawTriangleStrip);
        geometry->allocate(segment ? segment->vertices.size() : 0);
        if (segment) {
            auto *v = geometry->vertexDataAsPoint2D();
            for (int j = 0; j < segment->vertices.size(); ++j) {
                const auto point = segment->vertices.at(j); v[j].set(float(point.x()), float(point.y()));
            }
            static_cast<QSGFlatColorMaterial *>(node->material())->setColor(segment->color);
        }
        geometry->markVertexDataDirty(); node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }
    root->preview = preview; return root;
}
void TrajectoryItem::geometryChange(const QRectF &next, const QRectF &old)
{
    QQuickItem::geometryChange(next, old); requestPreview();
}
void TrajectoryItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return;
    beginDrag(event->position(), event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && orientationRect().contains(event->position()))); event->accept();
}
bool TrajectoryItem::beginDrag(const QPointF &position, bool rotate)
{
    endDrag();
    emit activated();
    if (rotate && planar()) return false;
    emit viewInteractionStarted();
    {
        QMutexLocker lock(&m_mutex);
        // Rotation uses the visible orientation. Panning retains pending pointer zooms.
        m_dragCamera = rotate && m_preview ? m_displayCamera : m_camera;
        m_dragSize = rotate && m_preview ? m_displaySize : QSizeF(width(), height());
        m_dragStart = position;
        m_dragScale = m_data && m_data->valid()
            ? TrajectoryBuilder::projectionScale(*m_data, m_dragCamera, m_dragSize) : qMin(width(), height());
        m_dragScale = qMax(1.0, m_dragScale);
        if (rotate && m_dragCamera.viewScale == 0)
            m_dragCamera.viewScale = m_dragScale / m_dragCamera.zoom / qMax(1.0, qMin(m_dragSize.width(), m_dragSize.height()));
    }
    m_pan = !rotate;
    m_dragging = true; requestPreview(); return true;
}
void TrajectoryItem::mouseMoveEvent(QMouseEvent *event)
{
    dragTo(event->position()); event->accept();
}
void TrajectoryItem::dragTo(const QPointF &position)
{
    if (!m_dragging) return;
    const auto delta = position - m_dragStart;
    if (delta.isNull()) return;
    auto view = m_dragCamera;
    if (m_pan) {
        view.panX += delta.x() / qMax(1.0, m_dragSize.width());
        view.panY += delta.y() / qMax(1.0, m_dragSize.height());
    } else {
        const auto turn = QQuaternion::fromAxisAndAngle(QVector3D(float(delta.y()), float(delta.x()), 0),
                                                       float(std::hypot(delta.x(), delta.y()) * .4));
        const auto orientation = (turn * m_dragCamera.orientation()).normalized();
        view.freeRotation = true;
        view.rotation = {{orientation.scalar(), orientation.x(), orientation.y(), orientation.z()}};
        // Rotate camera-space translation too: the viewport centre stays the pivot after panning.
        const auto translation = turn.rotatedVector(QVector3D(float(view.panX * m_dragSize.width() / m_dragScale),
            float(-view.panY * m_dragSize.height() / m_dragScale), float(view.panDepth)));
        view.panX = translation.x() * m_dragScale / qMax(1.0, m_dragSize.width());
        view.panY = -translation.y() * m_dragScale / qMax(1.0, m_dragSize.height());
        view.panDepth = translation.z();
    }
    setCamera(view);
}
void TrajectoryItem::mouseReleaseEvent(QMouseEvent *event)
{
    mouseUngrabEvent(); event->accept();
}
void TrajectoryItem::mouseUngrabEvent()
{
    endDrag();
}
void TrajectoryItem::endDrag()
{
    if (!m_dragging) return;
    m_dragging = false; requestPreview(); emit viewInteractionFinished();
}
void TrajectoryItem::wheelEvent(QWheelEvent *event)
{
    const double delta = event->angleDelta().y() ? event->angleDelta().y() : event->pixelDelta().y() * 2;
    if (delta == 0) return;
    zoomAt(event->position(), delta); event->accept();
}
void TrajectoryItem::zoomAt(const QPointF &position, double wheelDelta)
{
    if (!qIsFinite(wheelDelta) || !qIsFinite(position.x()) || !qIsFinite(position.y()) || wheelDelta == 0) return;
    emit activated();
    auto view = camera();
    const double zoom = qBound(.05, view.zoom * std::pow(1.15, wheelDelta / 120.0), 100.0);
    const double ratio = zoom / view.zoom;
    const double x = position.x() / qMax(1.0, width()) - .5, y = position.y() / qMax(1.0, height()) - .5;
    view.panX = x - ratio * (x - view.panX);
    view.panY = y - ratio * (y - view.panY);
    view.zoom = zoom;
    setCamera(view);
}
