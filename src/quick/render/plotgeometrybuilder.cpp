#include "plotgeometrybuilder.h"

#include <QtMath>

#include <algorithm>

bool PlotViewTransform::isValid() const
{
    return qIsFinite(xMinimum) && qIsFinite(xMaximum)
        && qIsFinite(yMinimum) && qIsFinite(yMaximum)
        && qIsFinite(width) && qIsFinite(height)
        && xMaximum > xMinimum && yMaximum > yMinimum
        && width > 0.0 && height > 0.0;
}

QPointF PlotViewTransform::map(const QPointF &point) const
{
    return {(point.x() - xMinimum) / (xMaximum - xMinimum) * width,
            height - (point.y() - yMinimum) / (yMaximum - yMinimum) * height};
}

namespace {

static bool hasUsableLength(const QPointF &vector)
{
    return vector.x() * vector.x() + vector.y() * vector.y() >= 1e-24;
}

static QPointF clampToBounds(const QPointF &point,
                             const PlotViewTransform &transform)
{
    return {qBound(0.0, point.x(), transform.width),
            qBound(0.0, point.y(), transform.height)};
}

} // namespace

GeometryResult PlotGeometryBuilder::build(const LodResult &lod,
                                          const GeometryRequest &request)
{
    GeometryResult result;
    if (!request.transform.isValid() || !qIsFinite(request.lineWidth))
        return result;

    const double halfWidth = qMax(1.0, request.lineWidth) * 0.5;
    for (const LodSegment &lodSegment : lod.segments) {
        if (lodSegment.points.size() < 2)
            continue;

        QVector<QPointF> projected;
        projected.reserve(lodSegment.points.size());
        bool valid = true;
        for (const QPointF &point : lodSegment.points) {
            const QPointF mapped = request.transform.map(point);
            if (!qIsFinite(mapped.x()) || !qIsFinite(mapped.y())) {
                valid = false;
                break;
            }
            projected.append(mapped);
        }
        if (!valid)
            continue;

        QVector<QPointF> vertices;
        vertices.reserve(projected.size() * 2);
        bool hasTangent = false;
        for (int i = 0; i < projected.size(); ++i) {
            std::optional<QPointF> previous;
            std::optional<QPointF> next;
            for (int j = i - 1; j >= 0; --j) {
                if (hasUsableLength(projected.at(i) - projected.at(j))) {
                    previous = projected.at(j);
                    break;
                }
            }
            for (int j = i + 1; j < projected.size(); ++j) {
                if (hasUsableLength(projected.at(j) - projected.at(i))) {
                    next = projected.at(j);
                    break;
                }
            }

            QPointF tangent;
            if (previous.has_value() && next.has_value())
                tangent = *next - *previous;
            else if (next.has_value())
                tangent = *next - projected.at(i);
            else if (previous.has_value())
                tangent = projected.at(i) - *previous;
            if (!hasUsableLength(tangent))
                continue;

            hasTangent = true;
            const double length = qSqrt(tangent.x() * tangent.x()
                                        + tangent.y() * tangent.y());
            const QPointF normal(-tangent.y() / length * halfWidth,
                                 tangent.x() / length * halfWidth);
            vertices.append(clampToBounds(projected.at(i) + normal,
                                          request.transform));
            vertices.append(clampToBounds(projected.at(i) - normal,
                                          request.transform));
        }

        if (hasTangent && vertices.size() == projected.size() * 2)
            result.segments.append({lodSegment.seriesId, lodSegment.color,
                                    std::move(vertices)});
    }
    return result;
}
