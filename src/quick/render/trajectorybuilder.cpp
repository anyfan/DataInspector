#include "trajectorybuilder.h"
#include <QtMath>
#include <QVariantMap>
#include <QLineF>
#include <algorithm>
#include <limits>

namespace {
// WGS84 ellipsoid, at height zero: horizontal projection does not depend on
// whether the recorded height is ellipsoidal, MSL, or relative to a datum.
std::array<double, 3> surfaceEcef(double latitude, double longitude)
{
    constexpr double a = 6378137.0, f = 1.0 / 298.257223563;
    constexpr double e2 = f * (2 - f);
    const double lat = qDegreesToRadians(latitude), lon = qDegreesToRadians(longitude);
    const double s = std::sin(lat), c = std::cos(lat);
    const double n = a / std::sqrt(1 - e2 * s * s);
    return {n * c * std::cos(lon), n * c * std::sin(lon), n * (1 - e2) * s};
}
QPointF rotatedDirection(const std::array<double, 3> &p, const TrajectoryCamera &camera)
{
    const auto rotated = camera.orientation().rotatedVector(QVector3D(float(p[0]), float(p[1]), float(p[2])));
    return {rotated.x(), -rotated.y()};
}
struct ScreenProjection {
    std::array<double, 3> center{}, horizontal{}, vertical{};
    double span = 1, scale = 1;
    QPointF offset;
    template<typename T> QPointF mapNormalized(const std::array<T, 3> &p) const {
        double x = 0, y = 0;
        for (int axis = 0; axis < 3; ++axis) { x += horizontal[axis] * p[axis]; y += vertical[axis] * p[axis]; }
        return offset + QPointF(x * scale, y * scale);
    }
    QPointF map(const std::array<double, 3> &p) const {
        std::array<double, 3> local;
        for (int axis = 0; axis < 3; ++axis) local[axis] = (p[axis] - center[axis]) / span;
        return mapNormalized(local);
    }
};
ScreenProjection screenProjection(const TrajectoryData &data, const TrajectoryCamera &camera, const QSizeF &size)
{
    ScreenProjection result;
    double span = 0;
    for (int axis = 0; axis < 3; ++axis) {
        span = qMax(span, data.maximum[axis] - data.minimum[axis]);
        result.center[axis] = data.minimum[axis] * .5 + data.maximum[axis] * .5;
    }
    result.span = span > 0 ? span : 1;
    if (data.planar) {
        result.horizontal[data.horizontalAxis] = 1; result.vertical[data.verticalAxis] = data.verticalAxis == 2 ? 1 : -1;
        if (data.geographic) { result.horizontal = {0, 1, 0}; result.vertical = {-1, 0, 0}; }
    } else {
        const auto matrix = camera.orientation().toRotationMatrix();
        for (int axis = 0; axis < 3; ++axis) {
            result.horizontal[axis] = matrix(0, axis); result.vertical[axis] = -matrix(1, axis);
        }
    }
    double extentX = 0, extentY = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const double fraction = (data.maximum[axis] - data.minimum[axis]) / result.span;
        extentX += std::abs(result.horizontal[axis]) * fraction;
        extentY += std::abs(result.vertical[axis]) * fraction;
    }
    // Fit the projected bounds to both canvas dimensions, with 6% margins.
    // A shallow aircraft track can now fill a wide subplot instead of being
    // restricted to half of its smaller dimension.
    const double fitX = extentX > 1e-12 ? size.width() * .88 / extentX : std::numeric_limits<double>::infinity();
    const double fitY = extentY > 1e-12 ? size.height() * .88 / extentY : std::numeric_limits<double>::infinity();
    double fit = qMin(fitX, fitY);
    if (!qIsFinite(fit)) fit = qMin(size.width(), size.height()) * .88;
    if (camera.viewScale > 0) fit = camera.viewScale * qMin(size.width(), size.height());
    result.scale = qMax(1.0, fit) * camera.zoom;
    result.offset = {size.width() * (.5 + camera.panX), size.height() * (.5 + camera.panY)};
    return result;
}
struct SegmentMetric {
    std::array<float, 3> first;
    double x, y, z, inverseLength;
    SegmentMetric(const std::array<float, 3> &a, const std::array<float, 3> &b) : first(a) {
        x = double(b[0]) - a[0]; y = double(b[1]) - a[1]; z = double(b[2]) - a[2];
        const double length = x * x + y * y + z * z;
        inverseLength = length > 0 ? 1 / length : 0;
    }
    double distanceSquared(const std::array<float, 3> &p) const {
        double dx = double(p[0]) - first[0], dy = double(p[1]) - first[1], dz = double(p[2]) - first[2];
        const double t = qBound(0.0, (dx * x + dy * y + dz * z) * inverseLength, 1.0);
        dx -= t * x; dy -= t * y; dz -= t * z;
        return dx * dx + dy * dy + dz * dz;
    }
};
bool buildLevels(TrajectoryData &data, const std::atomic_bool *cancel)
{
    constexpr std::array<double, 5> tolerances{{1e-6, 1e-5, 1e-4, 1e-3, 1e-2}};
    const qsizetype count = data.axes[data.timeAxis]->sampleCount();
    data.normalized.resize(count);
    const auto transform = screenProjection(data, {}, {1, 1});
    for (const auto &run : data.runs) for (qsizetype i = run.first; i <= run.second; ++i) {
        if ((i & 1023) == 0 && cancel && cancel->load()) return false;
        const auto p = TrajectoryBuilder::spatialPosition(data, i);
        for (int axis = 0; axis < 3; ++axis)
            data.normalized[i][axis] = float((p[axis] - transform.center[axis]) / transform.span);
    }
    QVector<double> importance(count, 0);
    struct Part { qsizetype first, last; double limit; };
    QVector<Part> stack;
    qint64 visits = 0;
    for (const auto &run : data.runs) {
        importance[run.first] = importance[run.second] = std::numeric_limits<double>::infinity();
        stack.append({run.first, run.second, std::numeric_limits<double>::infinity()});
        while (!stack.isEmpty()) {
            const auto part = stack.takeLast();
            if (part.last <= part.first + 1) continue;
            // Protect preparation against adversarial Douglas-Peucker O(N²).
            // Retain unresolved samples rather than losing their geometry.
            if (visits > qint64(count) * 64) {
                for (qsizetype i = part.first + 1; i < part.last; ++i) {
                    if ((i & 1023) == 0 && cancel && cancel->load()) return false;
                    importance[i] = part.limit;
                }
                continue;
            }
            double maximum = 0; qsizetype split = -1;
            const SegmentMetric metric(data.normalized[part.first], data.normalized[part.last]);
            for (qsizetype i = part.first + 1; i < part.last; ++i) {
                if ((++visits & 1023) == 0 && cancel && cancel->load()) return false;
                const double squared = metric.distanceSquared(data.normalized[i]);
                if (squared > maximum) { maximum = squared; split = i; }
            }
            if (maximum <= tolerances[0] * tolerances[0] || split < 0) continue;
            const double weight = qMin(part.limit, std::sqrt(maximum));
            importance[split] = weight;
            stack.append({split, part.last, weight}); stack.append({part.first, split, weight});
        }
    }
    for (double tolerance : tolerances) {
        TrajectoryData::Level level; level.tolerance = tolerance;
        level.runs.reserve(data.runs.size());
        for (const auto &run : data.runs) {
            QVector<qsizetype> indices;
            for (qsizetype i = run.first; i <= run.second; ++i) {
                if ((i & 1023) == 0 && cancel && cancel->load()) return false;
                if (importance[i] >= tolerance) indices.append(i);
            }
            level.count += indices.size(); level.runs.append(std::move(indices));
        }
        data.levels.append(std::move(level));
    }
    return true;
}
}

bool TrajectoryCamera::operator==(const TrajectoryCamera &o) const
{
    return azimuth == o.azimuth && elevation == o.elevation && zoom == o.zoom
        && panX == o.panX && panY == o.panY && freeRotation == o.freeRotation
        && (!freeRotation || rotation == o.rotation) && viewScale == o.viewScale && panDepth == o.panDepth;
}
QQuaternion TrajectoryCamera::orientation() const
{
    if (freeRotation) return QQuaternion(float(rotation[0]), float(rotation[1]), float(rotation[2]), float(rotation[3])).normalized();
    const double az = qDegreesToRadians(azimuth), el = qDegreesToRadians(elevation);
    QMatrix3x3 matrix;
    matrix(0, 0) = float(std::cos(az)); matrix(0, 1) = float(std::sin(az)); matrix(0, 2) = 0;
    matrix(1, 0) = float(-std::sin(el) * std::sin(az)); matrix(1, 1) = float(std::sin(el) * std::cos(az)); matrix(1, 2) = float(std::cos(el));
    matrix(2, 0) = float(std::cos(el) * std::sin(az)); matrix(2, 1) = float(-std::cos(el) * std::cos(az)); matrix(2, 2) = float(std::sin(el));
    // NED world axes mapped to the east/north/up projection basis.
    const auto nedToEnu = QQuaternion::fromAxisAndAngle(QVector3D(1, 1, 0), 180);
    return (QQuaternion::fromRotationMatrix(matrix) * nedToEnu).normalized();
}
QVector3D TrajectoryCamera::viewAngles() const
{
    // NED zero pose: north into screen, east right, down on screen (YZ front view).
    TrajectoryCamera zero; zero.azimuth = 0; zero.elevation = 0;
    const auto zeroPose = zero.orientation();
    const auto matrix = (zeroPose.conjugated() * orientation()).normalized().toRotationMatrix();
    const double cosY = std::hypot(double(matrix(0, 0)), double(matrix(1, 0)));
    const double y = std::atan2(-double(matrix(2, 0)), cosY);
    // At the Y singularity there is no unique X/Z split; use the equivalent Z=0 pose.
    const double x = cosY > 1e-6 ? std::atan2(double(matrix(2, 1)), double(matrix(2, 2)))
                                : std::atan2(-double(matrix(1, 2)), double(matrix(1, 1)));
    const double z = cosY > 1e-6 ? std::atan2(double(matrix(1, 0)), double(matrix(0, 0))) : 0;
    return QVector3D(float(qRadiansToDegrees(x)), float(qRadiansToDegrees(y)), float(qRadiansToDegrees(z)));
}
bool TrajectoryCamera::valid() const
{
    double norm = 0;
    if (freeRotation) for (double component : rotation) {
        if (!qIsFinite(component)) return false;
        norm += component * component;
    }
    return qIsFinite(azimuth) && azimuth >= -180 && azimuth <= 180
        && qIsFinite(elevation) && elevation >= -90 && elevation <= 90
        && qIsFinite(zoom) && zoom >= .05 && zoom <= 100
        && qIsFinite(panX) && std::abs(panX) <= MaximumTranslation
        && qIsFinite(panY) && std::abs(panY) <= MaximumTranslation
        && (!freeRotation || std::abs(norm - 1) < 1e-5)
        && qIsFinite(viewScale) && viewScale >= 0 && viewScale <= 1e6
        && qIsFinite(panDepth) && std::abs(panDepth) <= MaximumTranslation;
}
int TrajectoryRotationGizmo::pick(const QPointF &position, double *parameter) const
{
    if (radius <= 0 || !qIsFinite(position.x()) || !qIsFinite(position.y())) return -1;
    const double radial = QLineF(center, position).length();
    if (radial < radius * .16) return 0; // Keep a reliably grabbable free-rotation centre.
    const double tolerance = qMin(8.0, radius * .12);
    double closest = tolerance, depth = -std::numeric_limits<double>::infinity(), angle = 0;
    int picked = -1;
    for (int axis = 0; axis < 3; ++axis) {
        const auto &ring = rings[axis];
        for (qsizetype i = 1; i < ring.points.size(); ++i) {
            const QPointF a = ring.points[i - 1], delta = ring.points[i] - a;
            const double length2 = QPointF::dotProduct(delta, delta);
            if (length2 < 1e-12) continue;
            const double t = qBound(0.0, QPointF::dotProduct(position - a, delta) / length2, 1.0);
            const double distance = QLineF(position, a + delta * t).length();
            const double z = ring.depths[i - 1] * (1 - t) + ring.depths[i] * t;
            // At crossings prefer the front arc; elsewhere prefer the nearest stroke.
            if (distance <= tolerance && (picked < 0 || distance < closest - .75
                || (std::abs(distance - closest) <= .75 && z > depth))) {
                closest = distance; depth = z; picked = axis + 1;
                angle = (double(i - 1) + t) * 2 * M_PI / double(ring.points.size() - 1);
            }
        }
    }
    if (picked > 0 && parameter) *parameter = angle;
    return picked > 0 ? picked : radial <= radius ? 0 : -1;
}
std::shared_ptr<const TrajectoryRotationGizmo> TrajectoryBuilder::rotationGizmo(
    const TrajectoryCamera &camera, const QSizeF &size, const QColor &axisColor, int highlighted)
{
    auto result = std::make_shared<TrajectoryRotationGizmo>();
    if (size.isEmpty() || !camera.valid()) return result;
    // Share the origin of the corner direction triad; the world rotation pivot
    // remains the viewport centre and is independent of this control position.
    result->center = QPointF(qMin(55.0, size.width() * .25), size.height() - qMin(55.0, size.height() * .25));
    result->radius = qMin(44.0, qMin(size.width(), size.height()) * .18);
    const auto orientation = camera.orientation();
    constexpr int steps = 128;
    const std::array<QColor, 3> colors{{QColor("#e45b5b"), QColor("#36ac72"), QColor("#478fe0")}};
    for (int axis = 0; axis < 3; ++axis) {
        QVector3D u, v; u[(axis + 1) % 3] = 1; v[(axis + 2) % 3] = 1;
        const auto cu = orientation.rotatedVector(u), cv = orientation.rotatedVector(v);
        auto &ring = result->rings[axis];
        ring.u = QPointF(cu.x(), -cu.y()) * result->radius;
        ring.v = QPointF(cv.x(), -cv.y()) * result->radius;
        for (int i = 0; i <= steps; ++i) {
            const double angle = i * 2 * M_PI / steps, c = std::cos(angle), s = std::sin(angle);
            ring.points.append(result->center + ring.u * c + ring.v * s);
            ring.depths.append(cu.z() * c + cv.z() * s);
        }
    }
    const auto appendLines = [&](const QVector<QVector<QPointF>> &lines, const QColor &color, double width) {
        LodResult lod;
        for (const auto &line : lines) {
            LodSegment segment; segment.color = color; segment.lineWidth = width;
            for (const auto &point : line) segment.points.append({point.x(), size.height() - point.y()});
            lod.segments.append(std::move(segment));
        }
        auto geometry = PlotGeometryBuilder::build(lod, {{0, size.width(), 0, size.height(), size.width(), size.height()}, width});
        // Each colour/depth layer uses one reusable SG node, not one node per arc edge.
        GeometrySegment merged; merged.color = color; merged.triangleList = true;
        for (const auto &segment : geometry.segments) merged.vertices += segment.vertices;
        if (!merged.vertices.isEmpty()) result->geometry.segments.append(std::move(merged));
    };
    // Draw dim rear arcs first, then front arcs. The grabbed ring is drawn last.
    for (int front = 0; front < 2; ++front) for (int pass = 0; pass < 2; ++pass) for (int axis = 0; axis < 3; ++axis) {
        const bool selected = highlighted == axis + 1;
        if (selected != bool(pass)) continue;
        const auto &ring = result->rings[axis];
        QVector<QVector<QPointF>> lines;
        for (int i = 1; i <= steps; ++i) {
            if ((ring.depths[i - 1] + ring.depths[i] >= 0) == bool(front))
                lines.append({ring.points[i - 1], ring.points[i]});
        }
        QColor color = colors[axis]; color.setAlphaF(front ? (selected ? 1 : .8) : (selected ? .65 : .25));
        appendLines(lines, color, selected ? 4 : front ? 2 : 1.5);
    }
    // The preview already draws the direction triad at this same origin.
    appendLines({{result->center + QPointF(-3, 0), result->center + QPointF(3, 0)},
                 {result->center + QPointF(0, -3), result->center + QPointF(0, 3)}}, axisColor, 2);
    return result;
}
double TrajectoryBuilder::projectionScale(const TrajectoryData &data, const TrajectoryCamera &camera, const QSizeF &size)
{
    return screenProjection(data, camera, size).scale;
}
std::array<double, 3> TrajectoryBuilder::position(const TrajectoryData &data, qsizetype index)
{
    std::array<double, 3> result{};
    for (int axis = 0; axis < 3; ++axis) if (data.axes[axis]) result[axis] = data.axes[axis]->pointAt(index).y();
    return result;
}
std::array<double, 3> TrajectoryBuilder::spatialPosition(const TrajectoryData &data, qsizetype index)
{
    return data.geographic ? data.projected.at(index) : position(data, index);
}
std::shared_ptr<const TrajectoryData> TrajectoryBuilder::build(
    const std::array<PlotSeriesDataPtr, 3> &axes, const std::atomic_bool *cancel, bool geographic,
    const std::optional<std::array<double, 3>> &origin)
{
    auto data = std::make_shared<TrajectoryData>();
    data->axes = axes;
    data->geographic = geographic;
    auto fail = [&](const QString &message) { data->error = message; data->runs.clear(); return data; };
    QVector<int> selected;
    for (int axis = 0; axis < 3; ++axis) if (axes[axis]) selected.append(axis);
    if (selected.size() < 2) return fail(QStringLiteral("请选择至少两个坐标信号"));
    if (geographic && (!axes[0] || !axes[1])) return fail(QStringLiteral("经纬度模式必须选择纬度和经度；高度可留空"));
    data->planar = selected.size() == 2; data->timeAxis = selected.first();
    data->horizontalAxis = selected[0]; data->verticalAxis = selected[1];
    const qsizetype count = axes[data->timeAxis]->sampleCount();
    if (!count || std::any_of(selected.cbegin(), selected.cend(), [&](int axis) { return axes[axis]->sampleCount() != count; }))
        return fail(QStringLiteral("XYZ 样本数不同或为空；请使用相同时间基的信号"));
    data->minimum.fill(std::numeric_limits<double>::infinity());
    data->maximum.fill(-std::numeric_limits<double>::infinity());
    if (geographic) data->projected.resize(count);
    std::array<double, 3> originEcef{};
    double sinLat = 0, cosLat = 1, sinLon = 0, cosLon = 1;
    if (geographic) {
        // Choose a local origin near valid flight positions, retaining zero fixes as data.
        qsizetype originIndex = -1;
        for (qsizetype i = 0; i < count; ++i) {
            if ((i & 1023) == 0 && cancel && cancel->load()) return fail(QStringLiteral("已取消"));
            const auto raw = position(*data, i);
            if (!qIsFinite(axes[data->timeAxis]->pointAt(i).x()) || !qIsFinite(raw[0])
                || !qIsFinite(raw[1]) || !qIsFinite(raw[2])) continue;
            if (originIndex < 0) originIndex = i;
            if (raw[0] != 0 || raw[1] != 0) { originIndex = i; break; }
        }
        if (originIndex >= 0) {
            data->localOrigin = position(*data, originIndex);
            data->origin = origin.value_or(data->localOrigin); originEcef = surfaceEcef(data->origin[0], data->origin[1]);
            sinLat = std::sin(qDegreesToRadians(data->origin[0])); cosLat = std::cos(qDegreesToRadians(data->origin[0]));
            sinLon = std::sin(qDegreesToRadians(data->origin[1])); cosLon = std::cos(qDegreesToRadians(data->origin[1]));
        }
    }
    auto allMinimum = data->minimum, allMaximum = data->maximum;
    bool haveFitSample = false;
    qsizetype start = -1;
    double previousTime = -std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && cancel && cancel->load()) return fail(QStringLiteral("已取消"));
        const double t = axes[data->timeAxis]->pointAt(i).x();
        for (int axis : selected) {
            const double other = axes[axis]->pointAt(i).x();
            if (qIsFinite(t) != qIsFinite(other) || (qIsFinite(t) && t != other))
                return fail(QStringLiteral("XYZ 时间基不一致（包含时间偏移）；不进行插值或重采样"));
        }
        if (qIsFinite(t)) {
            if (t < previousTime) return fail(QStringLiteral("轨迹需要按时间非递减排列的原始样本"));
            previousTime = t;
        }
        auto p = position(*data, i);
        const bool includeInFit = !geographic || p[0] != 0 || p[1] != 0;
        const bool finite = qIsFinite(t) && qIsFinite(p[0]) && qIsFinite(p[1]) && qIsFinite(p[2]);
        if (!finite) {
            if (start >= 0) data->runs.append({start, i - 1});
            start = -1;
            continue;
        }
        if (geographic) {
            if (std::abs(p[0]) > 90 || std::abs(p[1]) > 180)
                return fail(QStringLiteral("经纬度超出范围：纬度须为 [-90,90]°，经度须为 [-180,180]°，高度单位为米"));
            const auto ecef = surfaceEcef(p[0], p[1]);
            const double dx = ecef[0] - originEcef[0], dy = ecef[1] - originEcef[1], dz = ecef[2] - originEcef[2];
            // NED: north/east tangent plane and downward recorded-height difference.
            p = {-sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz,
                 -sinLon * dx + cosLon * dy,
                 data->origin[2] - p[2]};
            if (!qIsFinite(p[0]) || !qIsFinite(p[1]) || !qIsFinite(p[2]))
                return fail(QStringLiteral("投影坐标超出可表示数值范围"));
            data->projected[i] = p;
        }
        if (start < 0) start = i;
        ++data->sampleCount;
        for (int axis = 0; axis < 3; ++axis) {
            allMinimum[axis] = qMin(allMinimum[axis], p[axis]); allMaximum[axis] = qMax(allMaximum[axis], p[axis]);
            if (includeInFit) {
                data->minimum[axis] = qMin(data->minimum[axis], p[axis]);
                data->maximum[axis] = qMax(data->maximum[axis], p[axis]);
            }
        }
        haveFitSample = haveFitSample || includeInFit;
    }
    if (start >= 0) data->runs.append({start, count - 1});
    if (data->runs.isEmpty()) return fail(QStringLiteral("没有同时有效的 XYZ 原始样本"));
    if (!haveFitSample) { data->minimum = allMinimum; data->maximum = allMaximum; }
    for (int axis = 0; axis < 3; ++axis)
        if (!qIsFinite(data->maximum[axis] - data->minimum[axis]))
            return fail(QStringLiteral("空间范围超出可表示数值范围"));
    if (!buildLevels(*data, cancel)) return fail(QStringLiteral("已取消"));
    return data;
}
QPointF TrajectoryBuilder::project(const TrajectoryData &data, const std::array<double, 3> &p,
                                  const TrajectoryCamera &camera, const QSizeF &size)
{
    return screenProjection(data, camera, size).map(p);
}
std::optional<qsizetype> TrajectoryBuilder::nearestSample(const TrajectoryData &data, double time)
{
    if (!data.valid() || !qIsFinite(time)) return std::nullopt;
    // Runs exclude missing time/coordinates. A query inside a gap never jumps across it.
    for (const auto &run : data.runs) {
        const auto &axis = *data.axes[data.timeAxis];
        const double first = axis.pointAt(run.first).x(), last = axis.pointAt(run.second).x();
        if (time < first || time > last) continue;
        return PlotSeriesStore::nearestSampleIndex(axis, time, run.first, run.second);
    }
    return std::nullopt;
}
std::shared_ptr<const TrajectoryFrame> TrajectoryBuilder::buildFrame(
    const QVector<TrajectorySource> &sources, const std::atomic_bool *cancel, const TrajectoryFrame *previous)
{
    auto frame = std::make_shared<TrajectoryFrame>();
    std::optional<std::array<double, 3>> origin;
    std::optional<bool> geographic;
    // Find an origin only after full validation, so a faulty first entry cannot
    // shift or invalidate the other trajectories. Hidden entries retain origin.
    for (const auto &source : sources) {
        std::shared_ptr<const TrajectoryData> data;
        const auto reuse = [&](const QVector<std::shared_ptr<const TrajectoryData>> &paths) {
            for (const auto &path : paths) if (path->axes == source.axes && path->geographic == source.geographic
                && (!source.geographic || (origin ? path->origin == *origin : path->origin == path->localOrigin))) { data = path; break; }
        };
        if (previous) reuse(previous->paths);
        if (!data) reuse(frame->paths);
        if (!data) data = build(source.axes, cancel, source.geographic, origin);
        if (data->valid()) {
            if (!geographic) geographic = source.geographic;
            if (source.geographic && !origin) origin = data->origin;
            if (*geographic != source.geographic) {
                auto failed = std::make_shared<TrajectoryData>();
                failed->error = QStringLiteral("同一子图不能混用地理与空间坐标"); data = failed;
            }
        }
        frame->paths.append(data);
        std::shared_ptr<const AttitudeData> attitude;
        if (previous) for (const auto &prepared : previous->attitudes)
            if (prepared->convention == source.attitude && prepared->sources == source.attitudeSources) { attitude = prepared; break; }
        frame->attitudes.append(attitude ? attitude : buildAttitude(source, cancel));
        if (cancel && cancel->load()) return frame;
    }
    std::shared_ptr<TrajectoryData> bounds;
    for (int i = 0; i < sources.size(); ++i) {
        const auto &data = frame->paths[i];
        if (!sources[i].visible || !data->valid()) continue;
        if (!bounds) {
            // Bounds carry only frame metadata; never duplicate sample caches.
            bounds = std::make_shared<TrajectoryData>();
            bounds->geographic = data->geographic; bounds->origin = data->origin;
            bounds->minimum = data->minimum; bounds->maximum = data->maximum;
            bounds->planar = data->planar; bounds->horizontalAxis = data->horizontalAxis;
            bounds->verticalAxis = data->verticalAxis; bounds->runs.append({0, 0});
        } else {
            bool representable = true;
            for (int a = 0; a < 3; ++a)
                representable = representable && qIsFinite(qMax(bounds->maximum[a], data->maximum[a]) - qMin(bounds->minimum[a], data->minimum[a]));
            if (!representable) {
                auto failed = std::make_shared<TrajectoryData>(); failed->error = QStringLiteral("与其他航迹的联合范围超出数值范围");
                frame->paths[i] = failed; continue;
            }
            bounds->planar = bounds->planar && data->planar
                && bounds->horizontalAxis == data->horizontalAxis && bounds->verticalAxis == data->verticalAxis;
            for (int a = 0; a < 3; ++a) {
                bounds->minimum[a] = qMin(bounds->minimum[a], data->minimum[a]);
                bounds->maximum[a] = qMax(bounds->maximum[a], data->maximum[a]);
            }
        }
    }
    frame->bounds = bounds;
    return frame;
}
std::shared_ptr<const AttitudeData> TrajectoryBuilder::buildAttitude(const TrajectorySource &source,
    const std::atomic_bool *cancel)
{
    auto result = std::make_shared<AttitudeData>();
    result->convention = source.attitude; result->sources = source.attitudeSources;
    if (!source.attitude.mode) return result;
    const int n = source.attitude.mode == 1 ? 3 : 4;
    auto fail = [&](const QString &error) { result->error = error; result->runs.clear(); return result; };
    for (int a = 0; a < n; ++a) if (!result->sources[a]) return fail(QStringLiteral("姿态来源未绑定或已移除"));
    const auto &first = *result->sources[0]; const auto count = first.sampleCount();
    for (int a = 1; a < n; ++a) if (result->sources[a]->sampleCount() != count)
        return fail(QStringLiteral("姿态分量样本数不一致"));
    qsizetype start = -1; double previous = -std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < count; ++i) {
        if ((i & 1023) == 0 && cancel && cancel->load()) return fail(QStringLiteral("已取消"));
        const double t = first.pointAt(i).x(); bool valid = qIsFinite(t); double norm = 0;
        for (int a = 0; a < n; ++a) {
            const auto p = result->sources[a]->pointAt(i);
            if (qIsFinite(t) != qIsFinite(p.x()) || (qIsFinite(t) && t != p.x()))
                return fail(QStringLiteral("姿态分量时间基不一致；不插值"));
            valid = valid && qIsFinite(p.y()); norm += p.y() * p.y();
        }
        if (qIsFinite(t)) { if (t < previous) return fail(QStringLiteral("姿态时间未排序")); previous = t; }
        if (n == 4) valid = valid && qIsFinite(norm) && norm > 1e-20;
        if (valid) { if (start < 0) start = i; }
        else if (start >= 0) { result->runs.append({start, i - 1}); start = -1; }
    }
    if (start >= 0) result->runs.append({start, count - 1});
    if (result->runs.isEmpty()) return fail(QStringLiteral("没有有效姿态原始样本"));
    return result;
}
std::optional<AttitudeSample> TrajectoryBuilder::attitudeSample(const AttitudeData &data, double time)
{
    if (!data.error.isEmpty() || !data.convention.mode || !qIsFinite(time)) return std::nullopt;
    const auto &axis = *data.sources[0];
    for (const auto &run : data.runs) {
        if (time < axis.pointAt(run.first).x() || time > axis.pointAt(run.second).x()) continue;
        const auto i = PlotSeriesStore::nearestSampleIndex(axis, time, run.first, run.second);
        if (!i) return std::nullopt;
        std::array<double, 4> values{};
        for (int a = 0; a < (data.convention.mode == 1 ? 3 : 4); ++a) values[a] = data.sources[a]->pointAt(*i).y();
        QQuaternion q;
        if (data.convention.mode == 1) {
            const double period = data.convention.radians ? 2 * M_PI : 360;
            const double factor = data.convention.radians ? 180 / M_PI : 1;
            for (int a = 0; a < 3; ++a) values[a] = std::remainder(values[a], period);
            const auto x = QQuaternion::fromAxisAndAngle(1, 0, 0, float(values[0] * factor));
            const auto y = QQuaternion::fromAxisAndAngle(0, 1, 0, float(values[1] * factor));
            const auto z = QQuaternion::fromAxisAndAngle(0, 0, 1, float(values[2] * factor));
            q = data.convention.order == 0 ? z * y * x : x * y * z;
        } else {
            const int w = data.convention.scalarLast ? 3 : 0;
            const int x = data.convention.scalarLast ? 0 : 1;
            double norm = 0; for (double v : values) norm += v * v;
            norm = std::sqrt(norm);
            q = QQuaternion(float(values[w] / norm), float(values[x] / norm), float(values[x + 1] / norm), float(values[x + 2] / norm));
        }
        if (data.convention.navigationToBody) q = q.conjugated();
        return AttitudeSample{q.normalized(), axis.pointAt(*i).x()};
    }
    return std::nullopt;
}
GeometryResult TrajectoryBuilder::attitudeGeometry(const QQuaternion &q, const TrajectoryData &bounds,
    const TrajectoryCamera &camera, const QSizeF &size, const QPointF &center, const QColor &color)
{
    const auto projection = screenProjection(bounds, camera, size);
    const auto map = [&](const QVector3D &body) {
        const auto ned = q.rotatedVector(body);
        double x = 0, y = 0;
        for (int a = 0; a < 3; ++a) { x += projection.horizontal[a] * ned[a]; y += projection.vertical[a] * ned[a]; }
        return center + QPointF(x, y) * 22; // logical pixels, independent of fit/zoom
    };
    // FRD arrow aircraft: nose +X, right wing +Y, belly +Z. Distinct
    // wing and fin surfaces keep roll readable, even in a front projection.
    const std::array<QVector3D, 7> vertices{{{1.4f, 0, 0}, {-.8f, 0, 0}, {-.25f, 1, 0},
        {-.25f, -1, 0}, {-.7f, 0, -.65f}, {-.65f, .45f, 0}, {-.65f, -.45f, 0}}};
    GeometryResult result;
    const std::array<std::array<int, 3>, 4> faces{{{{0, 1, 2}}, {{0, 3, 1}}, {{1, 4, 0}}, {{1, 5, 6}}}};
    for (int f = 0; f < int(faces.size()); ++f) {
        GeometrySegment segment; segment.triangleList = true;
        segment.color = f == 1 ? color.lighter(150) : f == 2 ? color.darker(160) : color;
        for (int index : faces[f]) segment.vertices.append(map(vertices[index]));
        result.segments.append(segment);
    }
    return result;
}
TrajectoryPreview TrajectoryBuilder::preview(const TrajectoryData &data, const TrajectoryCamera &camera,
    const QSizeF &size, double lineWidth, const QColor &axisColor, const std::atomic_bool *cancel, bool interactive,
    const TrajectoryData *bounds, bool drawAxes, const QColor &pathColor)
{
    TrajectoryPreview result;
    if (!data.valid() || size.isEmpty()) return result;
    const auto transform = screenProjection(bounds ? *bounds : data, camera, size);
    const auto local = screenProjection(data, camera, size);
    const double pixelTolerance = (interactive ? 1.0 : .25) / transform.scale * transform.span / local.span;
    const TrajectoryData::Level *level = nullptr;
    for (const auto &candidate : data.levels) {
        if (candidate.tolerance <= pixelTolerance || (interactive && candidate.count > 16000)) level = &candidate;
        else break;
    }
    if (interactive && !level) level = &data.levels.first();
    const qsizetype stride = interactive && level ? qMax(qsizetype(1), (level->count + 15999) / 16000) : 1;
    LodResult lod;
    auto addLine = [&](const QVector<QPointF> &points, const QColor &color, double width) {
        LodSegment s; s.color = color; s.lineWidth = width; s.points.reserve(points.size());
        for (const auto &point : points) s.points.append({point.x(), size.height() - point.y()});
        lod.segments.append(std::move(s));
    };
    for (qsizetype r = 0; r < data.runs.size(); ++r) {
        const auto &run = data.runs[r];
        const auto *indices = level ? &level->runs[r] : nullptr;
        const qsizetype count = indices ? indices->size() : run.second - run.first + 1;
        QVector<QPointF> points;
        points.reserve(qMin(qsizetype(100000), count / stride + 2));
        QPointF last;
        for (qsizetype j = 0; j < count;) {
            if ((result.projectedSamples & 1023) == 0 && cancel && cancel->load()) return {};
            const qsizetype i = indices ? indices->at(j) : run.first + j;
            const auto &p = data.normalized[i];
            std::array<double, 3> shared;
            for (int a = 0; a < 3; ++a) shared[a] = (double(p[a]) * local.span + local.center[a] - transform.center[a]) / transform.span;
            const auto screen = transform.mapNormalized(shared);
            ++result.projectedSamples;
            // Screen-space thinning keeps complete XYZ samples. Every omitted point
            // lies within .35 pixel of a retained point; never connect across a gap.
            const QPointF delta = screen - last;
            if (points.isEmpty() || i == run.second || QPointF::dotProduct(delta, delta) >= .1225) {
                points.append(screen); last = screen;
            }
            if (j == count - 1) break;
            j = qMin(count - 1, j + stride);
        }
        if (points.size() > 1) addLine(points, pathColor.isValid() ? pathColor : data.axes[data.timeAxis]->color, lineWidth);
    }
    if (drawAxes) {
    const auto &display = bounds ? *bounds : data;
    // A fixed-size orientation triad replaces the outer box. Only rotation
    // affects it: pan, zoom and data extents must not move or distort the gizmo.
    const double length = qMin(30.0, qMin(size.width(), size.height()) * .18);
    const QPointF base(qMin(55.0, size.width() * .25), size.height() - qMin(55.0, size.height() * .25));
    if (!display.planar)
        result.orientationRect = QRectF(base - QPointF(length + 10, length + 10), QSizeF(2 * length + 48, 2 * length + 36))
            .intersected(QRectF(QPointF(), size));
    const std::array<QColor, 3> colors{{QColor("#d94b4b"), QColor("#27945b"), QColor("#397bc5")}};
    addLine({base + QPointF(-2, 0), base + QPointF(2, 0)}, axisColor, 1);
    for (int axis = 0; axis < 3; ++axis) {
        if (display.planar && axis != display.horizontalAxis && axis != display.verticalAxis) continue;
        std::array<double, 3> unit{}; unit[axis] = 1;
        const auto delta = display.planar ? QPointF(transform.horizontal[axis] * length, transform.vertical[axis] * length)
                                      : rotatedDirection(unit, camera) * length;
        const auto end = base + delta;
        const double distance = std::hypot(delta.x(), delta.y());
        if (distance > 1) {
            addLine({base, end}, colors[axis], 2);
            const auto along = delta / distance;
            const QPointF normal(-along.y(), along.x());
            addLine({end - along * 5 + normal * 2.5, end, end - along * 5 - normal * 2.5}, colors[axis], 1.5);
        }
        const auto label = distance > 1 ? end : base + QPointF(-22, 5);
        const QString name = display.geographic ? QStringList{"X 北", "Y 东", "Z 地"}.at(axis)
                                             : QStringList{"X", "Y", "Z"}.at(axis);
        result.labels.append(QVariantMap{{"x", label.x()}, {"y", label.y()}, {"text", name}, {"color", colors[axis]}});
    }
    }
    result.geometry = PlotGeometryBuilder::build(lod,
        {{0, size.width(), 0, size.height(), size.width(), size.height()}, lineWidth});
    return result;
}
