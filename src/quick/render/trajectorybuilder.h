#pragma once
#include "plotgeometrybuilder.h"
#include <array>
#include <atomic>
#include <QSizeF>
#include <QVariantList>
#include <QQuaternion>

struct TrajectoryCamera {
    static constexpr double MaximumTranslation = 1e6;
    double azimuth = -45, elevation = 25, zoom = 1, panX = 0, panY = 0;
    // Optional free orientation, mapping world coordinates to camera coordinates.
    bool freeRotation = false;
    std::array<double, 4> rotation{{1, 0, 0, 0}}; // w, x, y, z
    double viewScale = 0, panDepth = 0; // 0 scale means fit; depth is normalized camera translation
    QQuaternion orientation() const;
    QVector3D viewAngles() const; // absolute NED angles, front YZ zero pose, Rz * Ry * Rx
    bool operator==(const TrajectoryCamera &o) const;
    bool valid() const;
};
struct TrajectoryData {
    std::array<PlotSeriesDataPtr, 3> axes;
    bool geographic = false; // raw axes are latitude degrees, longitude degrees, height metres
    std::array<double, 3> origin{};
    std::array<double, 3> localOrigin{}; // natural first fix, used to validate reuse after deleting origin owner
    QVector<std::array<double, 3>> projected; // immutable north/east/downward-height-difference cache
    QVector<std::array<float, 3>> normalized;
    struct Level {
        double tolerance = 0;
        QVector<QVector<qsizetype>> runs; // original sample indices, never bridge missing data
        qsizetype count = 0;
    };
    QVector<Level> levels;
    int timeAxis = 0, horizontalAxis = 0, verticalAxis = 1;
    bool planar = false;
    std::array<double, 3> minimum{}, maximum{};
    QVector<QPair<qsizetype, qsizetype>> runs; // inclusive ranges of finite XYZ samples
    QString error;
    qsizetype sampleCount = 0;
    bool valid() const { return error.isEmpty() && !runs.isEmpty(); }
};
// Measurement convention, independent of the world-to-camera orientation.
struct TrajectoryAttitude {
    int mode = 0; // 0 off, 1 roll/pitch/yaw, 2 quaternion
    std::array<int, 4> sources{{-1, -1, -1, -1}};
    bool radians = false, scalarLast = false, navigationToBody = false;
    int order = 0; // 0 Rz(yaw)*Ry(pitch)*Rx(roll), 1 Rx*Ry*Rz
    bool operator==(const TrajectoryAttitude &o) const { return mode == o.mode && sources == o.sources && radians == o.radians && scalarLast == o.scalarLast && navigationToBody == o.navigationToBody && order == o.order; }
};
struct AttitudeData {
    TrajectoryAttitude convention;
    std::array<PlotSeriesDataPtr, 4> sources;
    QVector<QPair<qsizetype, qsizetype>> runs;
    QString error;
};
struct AttitudeSample { QQuaternion bodyToNavigation; double time = 0; };
struct TrajectorySource {
    QString id, name;
    std::array<PlotSeriesDataPtr, 3> axes;
    bool geographic = false, visible = true;
    QColor color;
    double width = 2;
    TrajectoryAttitude attitude;
    std::array<PlotSeriesDataPtr, 4> attitudeSources;
    bool operator==(const TrajectorySource &o) const { return id == o.id && name == o.name && axes == o.axes && geographic == o.geographic && visible == o.visible && color == o.color && width == o.width && attitude == o.attitude && attitudeSources == o.attitudeSources; }
};
struct TrajectoryFrame {
    QVector<std::shared_ptr<const TrajectoryData>> paths;
    QVector<std::shared_ptr<const AttitudeData>> attitudes;
    std::shared_ptr<const TrajectoryData> bounds;
};
struct TrajectoryPreview {
    GeometryResult geometry;
    QRectF orientationRect; // hit area around the fixed on-screen orientation axes
    QVariantList labels;
    qsizetype projectedSamples = 0;
};
// Immutable screen-space CAD rotation handles, shared by painting and picking.
struct TrajectoryRotationGizmo {
    struct Ring {
        QPointF u, v; // projected, radius-scaled basis with cross(worldU, worldV) = axis
        QVector<QPointF> points;
        QVector<double> depths;
    };
    QPointF center;
    double radius = 0;
    std::array<Ring, 3> rings;
    GeometryResult geometry;
    // -1 outside, 0 free trackball, 1/2/3 fixed world X/Y/Z.
    int pick(const QPointF &position, double *parameter = nullptr) const;
};
class TrajectoryBuilder final {
public:
    static std::shared_ptr<const TrajectoryData> build(
        const std::array<PlotSeriesDataPtr, 3> &axes, const std::atomic_bool *cancel = nullptr,
        bool geographic = false, const std::optional<std::array<double, 3>> &origin = std::nullopt);
    static std::shared_ptr<const TrajectoryFrame> buildFrame(const QVector<TrajectorySource> &sources,
        const std::atomic_bool *cancel = nullptr, const TrajectoryFrame *previous = nullptr);
    static std::shared_ptr<const AttitudeData> buildAttitude(const TrajectorySource &source,
        const std::atomic_bool *cancel = nullptr);
    static std::optional<AttitudeSample> attitudeSample(const AttitudeData &data, double time);
    static GeometryResult attitudeGeometry(const QQuaternion &bodyToNavigation, const TrajectoryData &bounds,
        const TrajectoryCamera &camera, const QSizeF &size, const QPointF &center, const QColor &color);
    static QPointF project(const TrajectoryData &data, const std::array<double, 3> &position,
                           const TrajectoryCamera &camera, const QSizeF &size);
    static double projectionScale(const TrajectoryData &data, const TrajectoryCamera &camera, const QSizeF &size);
    static std::shared_ptr<const TrajectoryRotationGizmo> rotationGizmo(
        const TrajectoryCamera &camera, const QSizeF &size, const QColor &axisColor, int highlighted = -1);
    static std::array<double, 3> position(const TrajectoryData &data, qsizetype index);
    static std::array<double, 3> spatialPosition(const TrajectoryData &data, qsizetype index);
    static std::optional<qsizetype> nearestSample(const TrajectoryData &data, double time);
    static TrajectoryPreview preview(const TrajectoryData &data, const TrajectoryCamera &camera,
                                     const QSizeF &size, double lineWidth, const QColor &axisColor,
                                     const std::atomic_bool *cancel = nullptr, bool interactive = false,
                                     const TrajectoryData *bounds = nullptr, bool drawAxes = true,
                                     const QColor &pathColor = QColor());
};
