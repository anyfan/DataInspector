#include "plotlodbuilder.h"

#include <QtMath>

#include <algorithm>
#include <limits>

bool LodRequestKey::operator==(const LodRequestKey &other) const
{
    return storeGeneration == other.storeGeneration
        && orderedIds == other.orderedIds
        && xMinimum == other.xMinimum
        && xMaximum == other.xMaximum
        && bucketCount == other.bucketCount
        && algorithmVersion == other.algorithmVersion;
}

namespace {

struct IndexedPoint
{
    QPointF point;
    int index = -1;
};

static void appendReducedBucket(QVector<QPointF> &output,
                                const IndexedPoint &minimum,
                                const IndexedPoint &maximum)
{
    if (minimum.index < maximum.index) {
        output.append(minimum.point);
        if (minimum.index != maximum.index)
            output.append(maximum.point);
    } else {
        output.append(maximum.point);
        if (minimum.index != maximum.index)
            output.append(minimum.point);
    }
}

static void appendSeriesLod(const PlotSeriesData &series,
                            const LodRequestKey &key,
                            QVector<LodSegment> &output)
{
    const QVector<QPointF> &points = series.points;
    auto first = points.cbegin();
    auto last = points.cend();
    if (series.monotonicTime) {
        first = std::lower_bound(points.cbegin(), points.cend(), key.xMinimum,
                                 [](const QPointF &point, double x) {
                                     return point.x() < x;
                                 });
        last = std::upper_bound(first, points.cend(), key.xMaximum,
                                [](double x, const QPointF &point) {
                                    return x < point.x();
                                });
        if (first != points.cbegin())
            --first;
        if (last != points.cend())
            ++last;
    }

    LodSegment current;
    current.seriesId = series.id;
    current.color = series.color;
    current.lineWidth = series.lineWidth;
    current.lineStyle = series.lineStyle;
    int currentBucket = -1;
    std::optional<IndexedPoint> minimum;
    std::optional<IndexedPoint> maximum;

    const double bucketWidth = (key.xMaximum - key.xMinimum)
            / static_cast<double>(key.bucketCount);
    auto flushBucket = [&]() {
        if (minimum.has_value())
            appendReducedBucket(current.points, *minimum, *maximum);
        minimum.reset();
        maximum.reset();
    };
    auto flushSegment = [&]() {
        flushBucket();
        if (!current.points.isEmpty())
            output.append(std::move(current));
        current = LodSegment{series.id, series.color, {}, series.lineWidth,
                             series.lineStyle};
        currentBucket = -1;
    };

    for (auto it = first; it != last; ++it) {
        const int index = static_cast<int>(it - points.cbegin());
        const QPointF &point = *it;
        if (!qIsFinite(point.x()) || !qIsFinite(point.y())) {
            flushSegment();
            continue;
        }
        if (!series.monotonicTime
            && (point.x() < key.xMinimum || point.x() > key.xMaximum))
            continue;

        const int bucket = qBound(0, static_cast<int>(
                                      (point.x() - key.xMinimum) / bucketWidth),
                                  key.bucketCount - 1);
        if (bucket != currentBucket) {
            flushBucket();
            currentBucket = bucket;
        }

        const IndexedPoint candidate{point, index};
        if (!minimum.has_value()) {
            minimum = candidate;
            maximum = candidate;
        } else {
            if (point.y() < minimum->point.y())
                minimum = candidate;
            if (point.y() > maximum->point.y())
                maximum = candidate;
        }
    }
    flushSegment();
}

} // namespace

LodResult PlotLodBuilder::build(const PlotSeriesSnapshot &snapshot,
                                const LodRequestKey &key)
{
    LodResult result;
    result.key = key;
    if (key.storeGeneration != snapshot.generation
        || !qIsFinite(key.xMinimum) || !qIsFinite(key.xMaximum)
        || key.xMaximum <= key.xMinimum || key.bucketCount <= 0)
        return result;

    for (const PlotSeriesDataPtr &series : snapshot.series)
        if (series)
            appendSeriesLod(*series, key, result.segments);
    return result;
}

const LodResult &PlotLodCache::resolve(const PlotSeriesSnapshot &snapshot,
                                       const LodRequestKey &key)
{
    if (!m_result.has_value() || !(m_result->key == key)) {
        m_result = PlotLodBuilder::build(snapshot, key);
        ++m_rebuildCount;
    }
    return *m_result;
}

void PlotLodCache::clear()
{
    m_result.reset();
}
