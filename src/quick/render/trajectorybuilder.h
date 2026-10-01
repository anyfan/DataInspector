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
    bool operator==(const TrajectoryCamera &o) const;
    bool valid() const;
};
struct TrajectoryData {
    std::array<PlotSeriesDataPtr, 3> axes;
    bool geographic = false; // raw axes are latitude degrees, longitude degrees, height metres
    std::array<double, 3> origin{};
    QVector<std::array<double, 3>> projected; // immutable east/north/relative-height cache
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
struct TrajectoryPreview {
    GeometryResult geometry;
    QRectF orientationRect; // hit area around the fixed on-screen orientation axes
    QVariantList labels;
    qsizetype projectedSamples = 0;
};
class TrajectoryBuilder final {
public:
    static std::shared_ptr<const TrajectoryData> build(
        const std::array<PlotSeriesDataPtr, 3> &axes, const std::atomic_bool *cancel = nullptr,
        bool geographic = false);
    static QPointF project(const TrajectoryData &data, const std::array<double, 3> &position,
                           const TrajectoryCamera &camera, const QSizeF &size);
    static double projectionScale(const TrajectoryData &data, const TrajectoryCamera &camera, const QSizeF &size);
    static std::array<double, 3> position(const TrajectoryData &data, qsizetype index);
    static std::array<double, 3> spatialPosition(const TrajectoryData &data, qsizetype index);
    static std::optional<qsizetype> nearestSample(const TrajectoryData &data, double time);
    static TrajectoryPreview preview(const TrajectoryData &data, const TrajectoryCamera &camera,
                                     const QSizeF &size, double lineWidth, const QColor &axisColor,
                                     const std::atomic_bool *cancel = nullptr, bool interactive = false);
};
