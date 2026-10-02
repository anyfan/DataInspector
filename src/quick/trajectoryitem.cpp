#include "trajectoryitem.h"
#include <QMutexLocker>
#include <QThreadPool>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QHoverEvent>
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
    std::shared_ptr<const TrajectoryRotationGizmo> gizmo;
};
double ringAngle(const TrajectoryRotationGizmo::Ring &ring, const QPointF &center, const QPointF &position)
{
    const auto d = position - center;
    const double determinant = ring.u.x() * ring.v.y() - ring.u.y() * ring.v.x();
    return std::atan2((ring.u.x() * d.y() - ring.u.y() * d.x()) / determinant,
                      (d.x() * ring.v.y() - d.y() * ring.v.x()) / determinant);
}
}
TrajectoryItem::TrajectoryItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
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
QRectF TrajectoryItem::rotationRect() const
{
    QMutexLocker lock(&m_mutex);
    return m_gizmo ? QRectF(m_gizmo->center - QPointF(m_gizmo->radius, m_gizmo->radius),
                           QSizeF(m_gizmo->radius * 2, m_gizmo->radius * 2)) : QRectF();
}
QVector3D TrajectoryItem::viewAngles() const
{
    QMutexLocker lock(&m_mutex);
    return (m_preview ? m_displayCamera : m_camera).viewAngles();
}
bool TrajectoryItem::rotationGizmoVisible() const
{
    QMutexLocker lock(&m_mutex); return bool(m_visibleGizmo);
}
bool TrajectoryItem::nearRotationControl(const QPointF &position) const
{
    QMutexLocker lock(&m_mutex);
    return m_gizmo && (QLineF(position, m_gizmo->center).length() <= m_gizmo->radius + 10
        || (m_preview && m_preview->orientationRect.contains(position)));
}
int TrajectoryItem::rotationHandleAt(const QPointF &position) const
{
    QMutexLocker lock(&m_mutex);
    return m_gizmo ? m_gizmo->pick(position) : -1;
}
void TrajectoryItem::refreshGizmo()
{
    TrajectoryCamera shown; QSizeF size; bool valid;
    {
        QMutexLocker lock(&m_mutex);
        valid = m_data && m_data->valid() && !m_data->planar && bool(m_preview);
        shown = m_displayCamera; size = m_displaySize;
    }
    const auto gizmo = valid ? TrajectoryBuilder::rotationGizmo(shown, size, axisColor(), rotationHandle()) : nullptr;
    const bool show = m_dragging ? rotating() && m_dragFromAxes : m_gizmoExpanded;
    bool visibilityChanged;
    {
        QMutexLocker lock(&m_mutex);
        visibilityChanged = bool(m_visibleGizmo) != (bool(gizmo) && show);
        m_gizmo = gizmo; m_visibleGizmo = show ? gizmo : nullptr;
    }
    if (visibilityChanged) emit interactionChanged();
    update();
}
void TrajectoryItem::hoverAt(const QPointF &position)
{
    m_pointerPosition = position;
    if (m_dragging) return;
    const bool expanded = nearRotationControl(position);
    const int handle = rotationHandleAt(position);
    if (handle == m_hoverHandle && expanded == m_gizmoExpanded) return;
    m_hoverHandle = handle; m_gizmoExpanded = expanded; refreshGizmo(); emit interactionChanged();
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
    emit styleChanged(); refreshGizmo(); requestPreview();
}
void TrajectoryItem::setAxes(const std::array<PlotSeriesDataPtr, 3> &axes, bool geographic)
{
    if (m_axes == axes && m_geographic == geographic) return;
    endDrag(); m_hoverHandle = -1; m_gizmoExpanded = false;
    m_axes = axes; m_geographic = geographic;
    if (m_job) m_job->cancelled = true;
    // Do not expose a previous source's position or trajectory while a new one builds.
    { QMutexLocker lock(&m_mutex); m_data.reset(); m_preview.reset(); m_gizmo.reset(); m_visibleGizmo.reset(); }
    emit interactionChanged();
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
        { QMutexLocker lock(&m_mutex); m_data.reset(); m_preview.reset(); m_gizmo.reset(); m_visibleGizmo.reset(); }
        emit interactionChanged(); emit previewChanged(); emit markersChanged(); update(); return;
    }
    m_job = std::make_shared<Job>();
    const auto job = m_job;
    job->revision = m_revision; job->camera = camera(); job->size = QSizeF(width(), height());
    job->interactionEpoch = m_interactionEpoch;
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
    if (!job->cancelled && job->interactionEpoch == m_interactionEpoch
        && job->data && job->data->axes == m_axes && job->data->geographic == m_geographic) {
        QMutexLocker lock(&m_mutex);
        m_data = job->data; m_preview = job->preview;
        m_displayCamera = job->camera; m_displaySize = job->size;
    }
    if (job->revision != m_revision) start();
    refreshGizmo();
    if (!m_dragging) hoverAt(m_pointerPosition);
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
    std::shared_ptr<const TrajectoryRotationGizmo> gizmo;
    { QMutexLocker lock(&m_mutex); preview = m_preview; gizmo = m_visibleGizmo; }
    if (root->preview == preview && root->gizmo == gizmo) return root;
    const int pathCount = preview ? preview->geometry.segments.size() : 0;
    const int count = pathCount + (gizmo ? gizmo->geometry.segments.size() : 0);
    while (root->nodes.size() < count) {
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        node->setGeometry(geometry); node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGFlatColorMaterial); node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node); root->nodes.append(node);
    }
    for (int i = 0; i < root->nodes.size(); ++i) {
        if (i < pathCount && root->preview == preview) continue; // Hover only updates the handles.
        auto *node = root->nodes.at(i); auto *geometry = node->geometry();
        const GeometrySegment *segment = i < pathCount ? &preview->geometry.segments.at(i)
            : i < count ? &gizmo->geometry.segments.at(i - pathCount) : nullptr;
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
    root->preview = preview; root->gizmo = gizmo; return root;
}
void TrajectoryItem::geometryChange(const QRectF &next, const QRectF &old)
{
    QQuickItem::geometryChange(next, old); requestPreview();
}
void TrajectoryItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return;
    beginPointerDrag(event->position(), int(event->button()), int(event->modifiers())); event->accept();
}
bool TrajectoryItem::beginPointerDrag(const QPointF &position, int button, int modifiers)
{
    if (button != Qt::LeftButton && button != Qt::MiddleButton) return false;
    const bool isPlanar = planar();
    if (isPlanar && button == Qt::MiddleButton) { endDrag(); emit activated(); return false; }
    const bool shift = modifiers & Qt::ShiftModifier;
    const int handle = !isPlanar && button == Qt::LeftButton && !shift ? rotationHandleAt(position) : -1;
    const bool rotate = !isPlanar && !shift && (button == Qt::MiddleButton || handle >= 0
        || (modifiers & Qt::AltModifier) || orientationRect().contains(position));
    if (!beginDrag(position, rotate)) return false;
    if (rotate && handle > 0 && m_dragGizmo) {
        m_dragAxis = handle;
        const auto &ring = m_dragGizmo->rings[handle - 1];
        double parameter = 0;
        m_dragGizmo->pick(position, &parameter);
        const double determinant = ring.u.x() * ring.v.y() - ring.u.y() * ring.v.x();
        m_dragRingEdgeOn = std::abs(determinant) < m_dragGizmo->radius * m_dragGizmo->radius * .15;
        if (m_dragRingEdgeOn) {
            m_dragTangent = -ring.u * std::sin(parameter) + ring.v * std::cos(parameter);
            if (QPointF::dotProduct(m_dragTangent, m_dragTangent) < std::pow(m_dragGizmo->radius * .2, 2))
                m_dragTangent = QPointF::dotProduct(ring.u, ring.u) > QPointF::dotProduct(ring.v, ring.v) ? -ring.u : ring.v;
        } else {
            m_dragLastAngle = ringAngle(ring, m_dragGizmo->center, position);
            m_dragRingPosition = position;
        }
        refreshGizmo(); emit interactionChanged();
    }
    return true;
}
bool TrajectoryItem::beginDrag(const QPointF &position, bool rotate)
{
    if (!qIsFinite(position.x()) || !qIsFinite(position.y())) return false;
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
        m_pointerPosition = position;
        m_dragAxis = 0; m_dragAngle = 0; m_dragLastAngle = 0;
        m_dragGizmo = m_gizmo;
        m_dragFromAxes = (m_preview && m_preview->orientationRect.contains(position))
            || (m_gizmo && QLineF(position, m_gizmo->center).length() <= m_gizmo->radius + 10);
        m_dragScale = m_data && m_data->valid()
            ? TrajectoryBuilder::projectionScale(*m_data, m_dragCamera, m_dragSize) : qMin(width(), height());
        m_dragScale = qMax(1.0, m_dragScale);
        if (rotate && m_dragCamera.viewScale == 0)
            m_dragCamera.viewScale = m_dragScale / m_dragCamera.zoom / qMax(1.0, qMin(m_dragSize.width(), m_dragSize.height()));
    }
    m_pan = !rotate;
    m_dragging = true;
    if (rotate) {
        // A new gesture starts from the displayed frame. Pending frames from
        // the preceding gesture must not briefly overtake this new baseline.
        ++m_interactionEpoch;
        setCamera(m_dragCamera);
    }
    refreshGizmo(); emit interactionChanged(); requestPreview(); return true;
}
void TrajectoryItem::mouseMoveEvent(QMouseEvent *event)
{
    dragTo(event->position()); event->accept();
}
void TrajectoryItem::dragTo(const QPointF &position)
{
    if (!m_dragging || !qIsFinite(position.x()) || !qIsFinite(position.y())) return;
    m_pointerPosition = position;
    const auto delta = position - m_dragStart;
    auto view = m_dragCamera;
    if (m_pan) {
        view.panX += delta.x() / qMax(1.0, m_dragSize.width());
        view.panY += delta.y() / qMax(1.0, m_dragSize.height());
    } else {
        QQuaternion turn;
        if (m_dragAxis != 0) {
            const auto &ring = m_dragGizmo->rings[m_dragAxis - 1];
            if (m_dragRingEdgeOn) {
                m_dragAngle = QPointF::dotProduct(delta, m_dragTangent) / qMax(1.0, QPointF::dotProduct(m_dragTangent, m_dragTangent));
            } else {
                const auto center = m_dragGizmo->center;
                const auto previous = m_dragRingPosition;
                m_dragRingPosition = position;
                const auto fromCenter = position - center;
                const double deadZone2 = std::pow(m_dragGizmo->radius * .08, 2);
                if (QPointF::dotProduct(fromCenter, fromCenter) < deadZone2) return;
                const double angle = ringAngle(ring, m_dragGizmo->center, position);
                const auto segment = position - previous;
                const double length2 = QPointF::dotProduct(segment, segment);
                const double t = length2 > 0 ? qBound(0.0, QPointF::dotProduct(center - previous, segment) / length2, 1.0) : 0;
                const auto closest = previous + segment * t - center;
                if (QPointF::dotProduct(closest, closest) < deadZone2) {
                    // The angle at the ring centre is undefined. Rebase after
                    // crossing it instead of injecting a half-turn.
                    m_dragLastAngle = angle;
                    return;
                }
                m_dragAngle += std::remainder(angle - m_dragLastAngle, 2 * M_PI);
                m_dragLastAngle = angle;
            }
            QVector3D axis;
            axis[m_dragAxis - 1] = 1;
            // Fixed world axes, expressed in the displayed camera frame.
            axis = m_dragCamera.orientation().rotatedVector(axis);
            turn = QQuaternion::fromAxisAndAngle(axis, float(qRadiansToDegrees(m_dragAngle)));
        } else {
            const double radius = m_dragFromAxes && m_dragGizmo ? m_dragGizmo->radius
                : qMax(1.0, qMin(m_dragSize.width(), m_dragSize.height()) * .45);
            const QPointF center(m_dragSize.width() * .5, m_dragSize.height() * .5);
            // The corner triad acts as a handle for the centre of the trackball.
            const QPointF start = m_dragFromAxes ? center : m_dragStart;
            const auto sphere = [radius, center](const QPointF &point) {
                double x = (point.x() - center.x()) / radius;
                double y = (center.y() - point.y()) / radius;
                const double r2 = x * x + y * y;
                // Sphere / hyperbolic sheet gives continuous control beyond the rim.
                const double z = r2 <= .5 ? std::sqrt(1 - r2) : .5 / std::sqrt(r2);
                return QVector3D(float(x), float(y), float(z)).normalized();
            };
            turn = QQuaternion::rotationTo(sphere(start), sphere(start + delta));
        }
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
    if (m_dragAxis > 0) emit interactionChanged();
}
void TrajectoryItem::mouseReleaseEvent(QMouseEvent *event)
{
    mouseUngrabEvent(); hoverAt(event->position()); event->accept();
}
void TrajectoryItem::mouseUngrabEvent()
{
    endDrag();
}
void TrajectoryItem::endDrag()
{
    if (!m_dragging) return;
    m_dragging = false; m_dragGizmo.reset();
    m_gizmoExpanded = nearRotationControl(m_pointerPosition);
    m_hoverHandle = rotationHandleAt(m_pointerPosition);
    refreshGizmo(); emit interactionChanged(); requestPreview(); emit viewInteractionFinished();
}
void TrajectoryItem::hoverMoveEvent(QHoverEvent *event) { hoverAt(event->position()); }
void TrajectoryItem::hoverLeaveEvent(QHoverEvent *)
{
    hoverAt({-1e6, -1e6});
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
