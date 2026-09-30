#include "plotgeometrybuilder.h"

#include <QtMath>

#include <optional>
#include <array>
#include <QRectF>

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

static std::optional<QPair<QPointF, QPointF>> clipSegment(
        const QPointF &start, const QPointF &end, double left, double top,
        double right, double bottom)
{
    if (!qIsFinite(start.x()) || !qIsFinite(start.y())
        || !qIsFinite(end.x()) || !qIsFinite(end.y())
        || right < left || bottom < top)
        return std::nullopt;
    const QPointF delta = end - start;
    if (!qIsFinite(delta.x()) || !qIsFinite(delta.y()))
        return std::nullopt;
    double t0 = 0.0;
    double t1 = 1.0;
    const double p[] = {-delta.x(), delta.x(), -delta.y(), delta.y()};
    const double q[] = {start.x() - left, right - start.x(),
                        start.y() - top, bottom - start.y()};
    for (int i = 0; i < 4; ++i) {
        if (!qIsFinite(p[i]) || !qIsFinite(q[i]))
            return std::nullopt;
        if (qFuzzyIsNull(p[i])) {
            if (q[i] < 0.0) return std::nullopt;
            continue;
        }
        const double ratio = q[i] / p[i];
        if (!qIsFinite(ratio))
            return std::nullopt;
        if (p[i] < 0.0) {
            if (ratio > t1) return std::nullopt;
            t0 = qMax(t0, ratio);
        } else {
            if (ratio < t0) return std::nullopt;
            t1 = qMin(t1, ratio);
        }
    }
    if (t0 > t1)
        return std::nullopt;
    const QPointF clippedStart = start + delta * t0;
    const QPointF clippedEnd = start + delta * t1;
    if (!qIsFinite(clippedStart.x()) || !qIsFinite(clippedStart.y())
        || !qIsFinite(clippedEnd.x()) || !qIsFinite(clippedEnd.y()))
        return std::nullopt;
    return std::make_pair(clippedStart, clippedEnd);
}

static QVector<QVector<QPointF>> clipPolyline(const QVector<QPointF> &points,
                                              const PlotViewTransform &transform,
                                              double lineWidth)
{
    const double halfWidth = qMax(1.0, lineWidth) * 0.5;
    const double left = halfWidth;
    const double top = halfWidth;
    const double right = transform.width - halfWidth;
    const double bottom = transform.height - halfWidth;
    if (right < left || bottom < top) return {};

    QVector<QVector<QPointF>> result;
    for (int index = 1; index < points.size(); ++index) {
        const auto clipped = clipSegment(points.at(index - 1), points.at(index),
                                          left, top, right, bottom);
        if (!clipped.has_value()) continue;
        const QPointF start = clipped->first;
        const QPointF end = clipped->second;
        if (!hasUsableLength(end - start)) continue;
        result.append({start, end});
    }
    return result;
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

static std::optional<QVector<QPointF>> buildStraightBand(const QVector<QPointF> &projected,
                                                         double lineWidth)
{
    if (projected.size() != 2) return std::nullopt;
    const QPointF direction = projected.at(1) - projected.at(0);
    if (!hasUsableLength(direction)) return std::nullopt;
    const double length = qSqrt(QPointF::dotProduct(direction, direction));
    const double radius = qMax(1.0, lineWidth) * .5;
    const QPointF normal(-direction.y()/length*radius, direction.x()/length*radius);
    const QPointF start = projected.at(0);
    const QPointF end = projected.at(1);
    return QVector<QPointF>{start + normal, start - normal, end + normal, end - normal};
}

static void appendRoundJoins(QVector<QPointF> &triangles,
                             const QVector<QPointF> &points, double width,
                             const PlotViewTransform &transform)
{
    // Inscribed round joins cannot overshoot the half-width stroke envelope.
    // Axis-aligned vertices give dense extrema identical outward coverage,
    // regardless of the direction of the adjacent LOD segments.
    static const auto circle = [] {
        std::array<QPointF, 16> vertices;
        for (int i = 0; i < 16; ++i) {
            const double angle = qDegreesToRadians(i * 22.5);
            vertices[i] = QPointF(qCos(angle), qSin(angle));
        }
        return vertices;
    }();
    const double radius = qMax(1.0, width) * .5;
    for (qsizetype i = 1; i + 1 < points.size(); ++i) {
        const auto center = points[i];
        if (center.x() < radius || center.x() > transform.width - radius
            || center.y() < radius || center.y() > transform.height - radius)
            continue;
        const auto incoming = center - points[i - 1];
        const auto outgoing = points[i + 1] - center;
        const double cross = incoming.x() * outgoing.y() - incoming.y() * outgoing.x();
        if (qFuzzyIsNull(cross) && QPointF::dotProduct(incoming, outgoing) >= 0)
            continue;
        for (int j = 0; j < 16; ++j) {
            triangles.append(center);
            triangles.append(center + circle[j] * radius);
            triangles.append(center + circle[(j + 1) % 16] * radius);
        }
    }
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
        const qsizetype firstGeometrySegment = result.segments.size();

        const double width = qIsFinite(lodSegment.lineWidth)
                && lodSegment.lineWidth > 0.0
                ? lodSegment.lineWidth : request.lineWidth;
        // At subpixel density, isolated round extrema still have different
        // MSAA coverage from horizontal connections. Cover the reduced bucket
        // envelope uniformly, using only its retained original min/max samples.
        // A stale LOD expanded by zoom must never become a wide filled band.
        QVector<QPointF> envelope;
        QVector<QRectF> denseCoverage(lodSegment.points.size());
        if (lodSegment.lineStyle == Qt::SolidLine && lod.key.bucketCount > 0) {
            const double bucketPixels = (lod.key.xMaximum - lod.key.xMinimum)
                / (request.transform.xMaximum - request.transform.xMinimum)
                * request.transform.width / lod.key.bucketCount;
            const double radius = qMax(1.0, width) * .5;
            if (bucketPixels > 0 && bucketPixels <= 1.01) {
                for (const auto &bucket : lodSegment.denseBuckets) {
                    if (bucket.firstPoint < 0 || bucket.firstPoint + 1 >= lodSegment.points.size())
                        continue;
                    const auto a = request.transform.map(lodSegment.points[bucket.firstPoint]);
                    const auto b = request.transform.map(lodSegment.points[bucket.firstPoint + 1]);
                    // Sparse/low-amplitude detail remains an ordinary stroke.
                    if (qAbs(a.y() - b.y()) < 4 * qMax(1.0, width)) continue;
                    const double left = qMax(0.0, request.transform.map({bucket.firstX, 0}).x() - radius);
                    const double right = qMin(request.transform.width,
                        request.transform.map({bucket.lastX, 0}).x() + radius);
                    const double top = qMax(0.0, qMin(a.y(), b.y()) - radius);
                    const double bottom = qMin(request.transform.height, qMax(a.y(), b.y()) + radius);
                    if (!qIsFinite(left) || !qIsFinite(right) || !qIsFinite(top) || !qIsFinite(bottom)
                        || left >= right || top >= bottom) continue;
                    denseCoverage[bucket.firstPoint] = QRectF(left, top, right - left, bottom - top);
                    denseCoverage[bucket.firstPoint + 1] = denseCoverage[bucket.firstPoint];
                    envelope.append({left, top}); envelope.append({left, bottom});
                    envelope.append({right, top}); envelope.append({left, bottom});
                    envelope.append({right, bottom}); envelope.append({right, top});
                }
            }
        }
        auto emitProjected = [&](const QVector<QPointF> &projected) {
        if (projected.size() < 2)
            return;
        const QVector<QVector<QPointF>> pieces = strokePieces(
            projected, lodSegment.lineStyle, width);
        QVector<QPointF> triangles;
        for (const QVector<QPointF> &piece : pieces) {
            const QVector<QVector<QPointF>> clippedPieces = clipPolyline(
                piece, request.transform, width);
            for (const QVector<QPointF> &clippedPiece : clippedPieces) {
                for (int index = 1; index < clippedPiece.size(); ++index) {
                    auto vertices = buildStraightBand(
                        {clippedPiece.at(index - 1), clippedPiece.at(index)}, width);
                    if (!vertices.has_value() || vertices->size() < 4)
                        continue;
                    triangles.append(vertices->at(0));
                    triangles.append(vertices->at(1));
                    triangles.append(vertices->at(2));
                    triangles.append(vertices->at(1));
                    triangles.append(vertices->at(3));
                    triangles.append(vertices->at(2));
                }
            }
        }
        for (const auto &piece : pieces)
            appendRoundJoins(triangles, piece, width, request.transform);
        if (!triangles.isEmpty())
            result.segments.append({lodSegment.seriesId, lodSegment.color,
                                    std::move(triangles), true});
        };

        QVector<QPointF> projected;
        projected.reserve(lodSegment.points.size());
        for (qsizetype pointIndex = 0; pointIndex < lodSegment.points.size(); ++pointIndex) {
            const QPointF mapped = request.transform.map(lodSegment.points[pointIndex]);
            // Replace covered dense strokes instead of layering round caps over
            // the envelope. Keep bridges wherever the envelopes do not overlap.
            if (pointIndex > 0 && !projected.isEmpty()
                && denseCoverage[pointIndex].intersects(denseCoverage[pointIndex - 1])) {
                emitProjected(projected);
                projected.clear();
            }
            if (!qIsFinite(mapped.x()) || !qIsFinite(mapped.y())
                || qAbs(mapped.x()) >= 1e8 || qAbs(mapped.y()) >= 1e8) {
                emitProjected(projected);
                projected.clear();
                continue;
            }
            if (projected.isEmpty() || hasUsableLength(mapped - projected.last()))
                projected.append(mapped);
        }
        emitProjected(projected);
        if (!envelope.isEmpty()) {
            if (result.segments.size() > firstGeometrySegment)
                result.segments.last().vertices += envelope;
            else
                result.segments.append({lodSegment.seriesId, lodSegment.color, std::move(envelope), true});
        }
    }
    return result;
}
