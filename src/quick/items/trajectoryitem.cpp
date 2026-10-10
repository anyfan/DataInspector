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
    std::shared_ptr<const GeometryResult> attitudes;
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
    QStringList errors;
    if (m_frame) for (int i = 0; i < m_frame->paths.size(); ++i)
        if (m_sources[i].visible && !m_frame->paths[i]->error.isEmpty()) errors.append(m_sources[i].name + ": " + m_frame->paths[i]->error);
    if (!errors.isEmpty()) return errors.join("\n");
    return m_data ? m_data->error : QStringLiteral("请选择至少两个坐标信号或显示有效航迹");
}
QString TrajectoryItem::referenceOrigin() const
{
    QMutexLocker lock(&m_mutex);
    if (!m_data || !m_data->geographic) return QStringLiteral("空间 XYZ 共用坐标系（NED 米）");
    return QStringLiteral("公共原点：纬 %1° 经 %2° 高 %3 m（首条有效航迹；隐藏保留）")
        .arg(QString::number(m_data->origin[0], 'g', 12), QString::number(m_data->origin[1], 'g', 12), QString::number(m_data->origin[2], 'g', 12));
}
QString TrajectoryItem::attitudeStatus() const
{
    QMutexLocker lock(&m_mutex); QStringList status;
    if (!m_frame) return {};
    for (int i = 0; i < m_sources.size(); ++i) {
        if (!m_sources[i].visible || !m_sources[i].attitude.mode) continue;
        if (!m_cursorMode) { status.append(m_sources[i].name + QStringLiteral("：启用时间游标显示姿态")); continue; }
        for (int c = 0; c < m_cursorMode; ++c) {
            const double t = c == 0 ? m_t1 : m_t2;
            const auto &position = *m_frame->paths[i]; const auto &attitude = *m_frame->attitudes[i];
            QString reason;
            if (t < m_timeMinimum || t > m_timeMaximum) reason = QStringLiteral("游标在可见时间范围外");
            else if (!TrajectoryBuilder::nearestSample(position, t)) reason = QStringLiteral("位置缺口或时间范围外");
            else if (!TrajectoryBuilder::attitudeSample(attitude, t)) reason = attitude.error.isEmpty() ? QStringLiteral("姿态缺口或时间范围外") : attitude.error;
            if (!reason.isEmpty()) status.append(m_sources[i].name + QStringLiteral(" 游标 %1：姿态隐藏，").arg(c + 1) + reason);
        }
    }
    return status.join("\n");
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
    TrajectorySource source; source.axes = axes; source.geographic = geographic;
    source.width = lineWidth();
    setSources({source});
}
void TrajectoryItem::setSources(const QVector<TrajectorySource> &sources)
{
    if (m_sources == sources) return;
    endDrag(); m_hoverHandle = -1; m_gizmoExpanded = false;
    m_sources = sources;
    m_axes = sources.isEmpty() ? std::array<PlotSeriesDataPtr, 3>{} : sources.first().axes;
    m_geographic = !sources.isEmpty() && sources.first().geographic;
    if (m_job) m_job->cancelled = true;
    { QMutexLocker lock(&m_mutex); m_data.reset(); m_frame.reset(); m_preview.reset(); m_attitudeGeometry.reset(); m_gizmo.reset(); m_visibleGizmo.reset(); }
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
    if (m_sources.isEmpty() || width() <= 0 || height() <= 0) {
        m_poll.stop();
        { QMutexLocker lock(&m_mutex); m_data.reset(); m_frame.reset(); m_preview.reset(); m_attitudeGeometry.reset(); m_gizmo.reset(); m_visibleGizmo.reset(); }
        emit interactionChanged(); emit previewChanged(); emit markersChanged(); update(); return;
    }
    m_job = std::make_shared<Job>();
    const auto job = m_job;
    job->revision = m_revision; job->camera = camera(); job->size = QSizeF(width(), height());
    job->interactionEpoch = m_interactionEpoch;
    job->sources = m_sources;
    std::shared_ptr<const TrajectoryFrame> previous;
    { QMutexLocker lock(&m_mutex); previous = m_preparedFrame; }
    const auto color = axisColor(); const bool interactive = m_dragging;
    trajectoryPool().start([job, previous, color, interactive] {
        try {
            job->frame = TrajectoryBuilder::buildFrame(job->sources, &job->cancelled, previous.get());
            job->data = job->frame->bounds;
            auto preview = std::make_shared<TrajectoryPreview>();
            if (job->data && !job->cancelled) {
                bool axesDrawn = false;
                for (int i = 0; i < job->sources.size(); ++i) {
                    const auto &source = job->sources[i]; const auto &data = job->frame->paths[i];
                    if (!source.visible || !data->valid()) continue;
                    auto part = TrajectoryBuilder::preview(*data, job->camera, job->size, source.width,
                        color, &job->cancelled, interactive, job->data.get(), !axesDrawn, source.color);
                    if (!axesDrawn) { preview->labels = part.labels; preview->orientationRect = part.orientationRect; axesDrawn = true; }
                    preview->projectedSamples += part.projectedSamples;
                    preview->geometry.segments += part.geometry.segments;
                }
            }
            job->preview = preview;
        } catch (...) {
            auto failed = std::make_shared<TrajectoryData>();
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
        && job->sources == m_sources) {
        QMutexLocker lock(&m_mutex);
        m_data = job->data; m_frame = job->frame; m_preparedFrame = job->frame; m_preview = job->preview;
        m_displayCamera = job->camera; m_displaySize = job->size;
    }
    if (job->revision != m_revision) start();
    refreshGizmo();
    if (!m_dragging) hoverAt(m_pointerPosition);
    refreshAttitudes(); emit previewChanged(); emit markersChanged(); update();
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
    refreshAttitudes(); emit markersChanged(); update();
}
QVariantList TrajectoryItem::markers() const
{
    QMutexLocker lock(&m_mutex);
    QVariantList result;
    if (!m_frame || !m_data || !m_data->valid() || !m_preview) return result;
    for (int path = 0; path < m_frame->paths.size(); ++path) {
        const auto &data = *m_frame->paths[path]; const auto &source = m_sources[path];
        if (!source.visible || !data.valid()) continue;
        const auto append = [&](qsizetype index, const QString &label, const QColor &color, int cursor) {
            const auto xyz = TrajectoryBuilder::position(data, index);
            const auto spatial = TrajectoryBuilder::spatialPosition(data, index);
            const auto screen = TrajectoryBuilder::project(*m_data, spatial, m_displayCamera, m_displaySize);
            const double time = data.axes[data.timeAxis]->pointAt(index).x();
            QString details = source.name + QStringLiteral(" %1  t=%2").arg(label, QString::number(time, 'g', 12));
            const QStringList names = data.geographic ? QStringList{QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("高度")} : QStringList{"X", "Y", "Z"};
            for (int axis = 0; axis < 3; ++axis) if (data.axes[axis]) {
                details += QStringLiteral("  %1=%2").arg(names[axis], QString::number(xyz[axis], 'g', 12));
                if (data.geographic) details += axis == 2 ? QStringLiteral(" m") : QStringLiteral("°");
            }
            QVariantMap marker{{"x", screen.x()}, {"y", screen.y()}, {"color", color}, {"trajectory", source.id},
                {"text", label}, {"name", source.name}, {"details", details}, {"time", time}, {"cursor", cursor},
                {"spatialX", spatial[0]}, {"spatialY", spatial[1]}, {"spatialZ", spatial[2]},
                {"rawX", data.axes[0] ? QVariant(xyz[0]) : QVariant()},
                {"rawY", data.axes[1] ? QVariant(xyz[1]) : QVariant()},
                {"rawZ", data.axes[2] ? QVariant(xyz[2]) : QVariant()}};
            if (cursor >= 0 && source.attitude.mode) {
                const auto &attitude = *m_frame->attitudes[path];
                const double requested = cursor == 0 ? m_t1 : m_t2;
                const auto sample = TrajectoryBuilder::attitudeSample(attitude, requested);
                if (sample) {
                    marker["attitudeTime"] = sample->time; marker["attitudeDelta"] = sample->time - time;
                    const auto q = sample->bodyToNavigation;
                    marker["attitude"] = QVariantList{q.scalar(), q.x(), q.y(), q.z()};
                    details += QStringLiteral("  姿态 t=%1 Δt(姿态−位置)=%2 s").arg(QString::number(sample->time, 'g', 12), QString::number(sample->time - time, 'g', 8));
                } else details += QStringLiteral("  姿态隐藏：") + (attitude.error.isEmpty() ? QStringLiteral("游标位于姿态缺口或时间范围外") : attitude.error);
                marker["details"] = details;
            }
            result.append(marker);
        };
        append(data.runs.first().first, QStringLiteral("起点"), QColor("#1aa05b"), -1);
        append(data.runs.last().second, QStringLiteral("终点"), QColor("#e34d59"), -1);
        for (int cursor = 0; cursor < m_cursorMode; ++cursor) {
            const double time = cursor == 0 ? m_t1 : m_t2;
            if (time < m_timeMinimum || time > m_timeMaximum) continue;
            const auto sample = TrajectoryBuilder::nearestSample(data, time);
            if (sample) append(*sample, QStringLiteral("游标 %1").arg(cursor + 1), QColor(cursor == 0 ? "#e34d59" : "#4e79e7"), cursor);
        }
    }
    return result;
}
void TrajectoryItem::refreshAttitudes()
{
    auto geometry = std::make_shared<GeometryResult>();
    const auto readings = markers();
    {
        QMutexLocker lock(&m_mutex);
        if (m_data) for (const auto &entry : readings) {
            const auto marker = entry.toMap(); const auto q = marker.value("attitude").toList();
            if (q.size() != 4) continue;
            const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(), [&](const TrajectorySource &s) { return s.id == marker.value("trajectory").toString(); });
            const auto part = TrajectoryBuilder::attitudeGeometry(QQuaternion(q[0].toFloat(), q[1].toFloat(), q[2].toFloat(), q[3].toFloat()),
                *m_data, m_displayCamera, m_displaySize, {marker.value("x").toDouble(), marker.value("y").toDouble()},
                source != m_sources.cend() && source->color.isValid() ? source->color : QColor("#ef971b"));
            geometry->segments += part.segments;
        }
        m_attitudeGeometry = geometry;
    }
}
QSGNode *TrajectoryItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *root = oldNode ? static_cast<TrajectoryRoot *>(oldNode) : new TrajectoryRoot;
    std::shared_ptr<const TrajectoryPreview> preview;
    std::shared_ptr<const TrajectoryRotationGizmo> gizmo;
    std::shared_ptr<const GeometryResult> attitudes;
    { QMutexLocker lock(&m_mutex); preview = m_preview; gizmo = m_visibleGizmo; attitudes = m_attitudeGeometry; }
    if (root->preview == preview && root->gizmo == gizmo && root->attitudes == attitudes) return root;
    const int pathCount = preview ? preview->geometry.segments.size() : 0;
    const int gizmoCount = gizmo ? gizmo->geometry.segments.size() : 0;
    const int count = pathCount + gizmoCount + (attitudes ? attitudes->segments.size() : 0);
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
            : i < pathCount + gizmoCount ? &gizmo->geometry.segments.at(i - pathCount)
            : i < count ? &attitudes->segments.at(i - pathCount - gizmoCount) : nullptr;
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
    root->preview = preview; root->gizmo = gizmo; root->attitudes = attitudes; return root;
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
