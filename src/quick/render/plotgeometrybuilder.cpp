#include "plotgeometrybuilder.h"

#include <QtMath>

#include <algorithm>
#include <optional>

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

static QVector<double> dashPattern(Qt::PenStyle style, double width)
{
    const double unit = qMax(1.0, width);
    switch (style) {
    case Qt::DashLine: return {4.0 * unit, 2.0 * unit};
    case Qt::DotLine: return {unit, 2.0 * unit};
    case Qt::DashDotLine: return {4.0 * unit, 2.0 * unit, unit, 2.0 * unit};
    case Qt::DashDotDotLine:
        return {4.0 * unit, 2.0 * unit, unit, 2.0 * unit, unit, 2.0 * unit};
    default: return {};
    }
}

static QVector<QVector<QPointF>> strokePieces(const QVector<QPointF> &points,
                                              Qt::PenStyle style,
                                              double width)
{
    const QVector<double> pattern = dashPattern(style, width);
    if (pattern.isEmpty()) return {points};

    QVector<QVector<QPointF>> result;
    QVector<QPointF> current;
    int patternIndex = 0;
    double remaining = pattern.first();
    bool drawing = true;
    for (int index = 1; index < points.size(); ++index) {
        QPointF start = points.at(index - 1);
        const QPointF end = points.at(index);
        QPointF delta = end - start;
        double segmentLength = qSqrt(QPointF::dotProduct(delta, delta));
        if (segmentLength <= 1e-12) continue;
        const QPointF direction = delta / segmentLength;
        while (segmentLength > 1e-12) {
            const double step = qMin(segmentLength, remaining);
            const QPointF next = start + direction * step;
            if (drawing) {
                if (current.isEmpty()) current.append(start);
                current.append(next);
            }
            start = next;
            segmentLength -= step;
            remaining -= step;
            if (remaining <= 1e-12) {
                if (drawing && current.size() >= 2)
                    result.append(std::move(current));
                current.clear();
                patternIndex = (patternIndex + 1) % pattern.size();
                remaining = pattern.at(patternIndex);
                drawing = (patternIndex % 2) == 0;
            }
        }
    }
    if (drawing && current.size() >= 2) result.append(std::move(current));
    return result;
}

static std::optional<QVector<QPointF>> buildBand(const QVector<QPointF> &projected,
                                                 double lineWidth,
                                                 const PlotViewTransform &transform)
{
    if (projected.size() < 2) return std::nullopt;
    const double halfWidth = qMax(1.0, lineWidth) * 0.5;
    QVector<QPointF> vertices;
    vertices.reserve(projected.size() * 2);
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
        if (previous.has_value() && next.has_value()) tangent = *next - *previous;
        else if (next.has_value()) tangent = *next - projected.at(i);
        else if (previous.has_value()) tangent = projected.at(i) - *previous;
        if (!hasUsableLength(tangent)) return std::nullopt;
        const double length = qSqrt(QPointF::dotProduct(tangent, tangent));
        const QPointF normal(-tangent.y() / length * halfWidth,
                             tangent.x() / length * halfWidth);
        vertices.append(clampToBounds(projected.at(i) + normal, transform));
        vertices.append(clampToBounds(projected.at(i) - normal, transform));
    }
    return vertices;
}

} // namespace

GeometryResult PlotGeometryBuilder::build(const LodResult &lod,
                                          const GeometryRequest &request)
{
    GeometryResult result;
    if (!request.transform.isValid() || !qIsFinite(request.lineWidth))
        return result;

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

        const double width = qIsFinite(lodSegment.lineWidth)
                && lodSegment.lineWidth > 0.0
                ? lodSegment.lineWidth : request.lineWidth;
        const QVector<QVector<QPointF>> pieces = strokePieces(
            projected, lodSegment.lineStyle, width);
        if (pieces.size() == 1 && lodSegment.lineStyle == Qt::SolidLine) {
            auto vertices = buildBand(pieces.first(), width, request.transform);
            if (vertices.has_value())
                result.segments.append({lodSegment.seriesId, lodSegment.color,
                                        std::move(*vertices), false});
            continue;
        }

        QVector<QPointF> triangles;
        for (const QVector<QPointF> &piece : pieces) {
            auto vertices = buildBand(piece, width, request.transform);
            if (!vertices.has_value()) continue;
            for (int index = 0; index + 3 < vertices->size(); index += 2) {
                triangles.append(vertices->at(index));
                triangles.append(vertices->at(index + 1));
                triangles.append(vertices->at(index + 2));
                triangles.append(vertices->at(index + 1));
                triangles.append(vertices->at(index + 3));
                triangles.append(vertices->at(index + 2));
            }
        }
        if (!triangles.isEmpty())
            result.segments.append({lodSegment.seriesId, lodSegment.color,
                                    std::move(triangles), true});
    }
    return result;
}
