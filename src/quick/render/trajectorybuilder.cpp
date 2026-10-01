#include "trajectorybuilder.h"
#include <QtMath>
#include <QVariantMap>
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
        result.horizontal[data.horizontalAxis] = 1; result.vertical[data.verticalAxis] = -1;
    } else if (!camera.freeRotation) {
        const double az = qDegreesToRadians(camera.azimuth), el = qDegreesToRadians(camera.elevation);
        result.horizontal = {std::cos(az), std::sin(az), 0};
        result.vertical = {std::sin(el) * std::sin(az), -std::sin(el) * std::cos(az), -std::cos(el)};
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
    return QQuaternion::fromRotationMatrix(matrix).normalized();
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
    const std::array<PlotSeriesDataPtr, 3> &axes, const std::atomic_bool *cancel, bool geographic)
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
            data->origin = position(*data, originIndex); originEcef = surfaceEcef(data->origin[0], data->origin[1]);
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
            // ECEF delta dotted with the origin's east/north unit vectors. Keep
            // recorded height differences as Z, without earth-curvature sag.
            p = {-sinLon * dx + cosLon * dy,
                 -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz,
                 p[2] - data->origin[2]};
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
TrajectoryPreview TrajectoryBuilder::preview(const TrajectoryData &data, const TrajectoryCamera &camera,
    const QSizeF &size, double lineWidth, const QColor &axisColor, const std::atomic_bool *cancel, bool interactive)
{
    TrajectoryPreview result;
    if (!data.valid() || size.isEmpty()) return result;
    const auto transform = screenProjection(data, camera, size);
    const double pixelTolerance = (interactive ? 1.0 : .25) / transform.scale;
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
            const auto screen = transform.mapNormalized(data.normalized[i]);
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
        if (points.size() > 1) addLine(points, data.axes[data.timeAxis]->color, lineWidth);
    }
    // A fixed-size orientation triad replaces the outer box. Only rotation
    // affects it: pan, zoom and data extents must not move or distort the gizmo.
    const double length = qMin(30.0, qMin(size.width(), size.height()) * .18);
    const QPointF base(qMin(55.0, size.width() * .25), size.height() - qMin(55.0, size.height() * .25));
    if (!data.planar)
        result.orientationRect = QRectF(base - QPointF(length + 10, length + 10), QSizeF(2 * length + 48, 2 * length + 36))
            .intersected(QRectF(QPointF(), size));
    const std::array<QColor, 3> colors{{QColor("#d94b4b"), QColor("#27945b"), QColor("#397bc5")}};
    addLine({base + QPointF(-2, 0), base + QPointF(2, 0)}, axisColor, 1);
    for (int axis = 0; axis < 3; ++axis) {
        if (data.planar && axis != data.horizontalAxis && axis != data.verticalAxis) continue;
        std::array<double, 3> unit{}; unit[axis] = 1;
        const auto delta = data.planar ? QPointF(axis == data.horizontalAxis ? length : 0, axis == data.verticalAxis ? -length : 0)
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
        const QString name = data.geographic ? QStringList{"X 东", "Y 北", "Z 高"}.at(axis)
                                             : QStringList{"X", "Y", "Z"}.at(axis);
        result.labels.append(QVariantMap{{"x", label.x()}, {"y", label.y()}, {"text", name}, {"color", colors[axis]}});
    }
    result.geometry = PlotGeometryBuilder::build(lod,
        {{0, size.width(), 0, size.height(), size.width(), size.height()}, lineWidth});
    return result;
}
