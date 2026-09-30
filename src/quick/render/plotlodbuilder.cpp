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
    qsizetype index = -1;
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
                            QVector<LodSegment> &output,
                            const std::atomic_bool *cancelled)
{
    const qsizetype pointCount = series.sampleCount();
    qsizetype firstIndex = 0;
    qsizetype lastIndex = pointCount;
    if (series.monotonicTime) {
        auto lowerBound = [&](double target) {
            qsizetype low = 0;
            qsizetype high = pointCount;
            while (low < high) {
                const qsizetype middle = low + (high - low) / 2;
                if (series.pointAt(middle).x() < target) low = middle + 1;
                else high = middle;
            }
            return low;
        };
        auto upperBound = [&](double target) {
            qsizetype low = 0;
            qsizetype high = pointCount;
            while (low < high) {
                const qsizetype middle = low + (high - low) / 2;
                if (target < series.pointAt(middle).x()) high = middle;
                else low = middle + 1;
            }
            return low;
        };
        firstIndex = lowerBound(key.xMinimum);
        lastIndex = upperBound(key.xMaximum);
        if (firstIndex > 0) --firstIndex;
        if (lastIndex < pointCount) ++lastIndex;
    }

    LodSegment current;
    current.seriesId = series.id;
    current.color = series.color;
    current.lineWidth = series.lineWidth;
    current.lineStyle = series.lineStyle;
    int currentBucket = -1;
    std::optional<IndexedPoint> minimum;
    std::optional<IndexedPoint> maximum;
    qsizetype bucketSamples = 0;
    double firstX = 0, lastX = 0;

    const double span = key.xMaximum - key.xMinimum;
    auto flushBucket = [&]() {
        if (minimum.has_value()) {
            // Keep the actual sample extent (not the whole bin) so gaps and
            // viewport neighbors are not filled by an envelope rectangle.
            if (series.monotonicTime && bucketSamples >= 4
                && minimum->index != maximum->index
                && firstX >= key.xMinimum && lastX <= key.xMaximum)
                current.denseBuckets.append({firstX, lastX, current.points.size()});
            appendReducedBucket(current.points, *minimum, *maximum);
        }
        bucketSamples = 0;
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

    for (qsizetype position = firstIndex; position < lastIndex; ++position) {
        if ((position & 1023) == 0 && cancelled && cancelled->load()) return;
        const qsizetype index = position;
        const QPointF point = series.pointAt(position);
        if (!qIsFinite(point.x()) || !qIsFinite(point.y())) {
            flushSegment();
            continue;
        }
        if (!series.monotonicTime
            && (point.x() < key.xMinimum || point.x() > key.xMaximum))
            continue;

        // Clamp the floating-point domain before converting to int. A distant
        // neighbor may overflow the subtraction; a tiny bucket width may be zero.
        const double fraction = point.x() <= key.xMinimum ? 0.0
            : point.x() >= key.xMaximum ? 1.0 : (point.x() - key.xMinimum) / span;
        const int bucket = static_cast<int>(qBound(0.0, fraction * key.bucketCount,
                                                    double(key.bucketCount - 1)));
        if (bucket != currentBucket) {
            flushBucket();
            currentBucket = bucket;
        }

        const IndexedPoint candidate{point, index};
        if (bucketSamples++ == 0) firstX = point.x();
        lastX = point.x();
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
                                const LodRequestKey &key,
                                const std::atomic_bool *cancelled)
{
    LodResult result;
    result.key = key;
    if (key.storeGeneration != snapshot.generation
        || !qIsFinite(key.xMinimum) || !qIsFinite(key.xMaximum)
        || key.xMaximum <= key.xMinimum || !qIsFinite(key.xMaximum - key.xMinimum)
        || key.bucketCount <= 0)
        return result;

    for (const PlotSeriesDataPtr &series : snapshot.series)
        if (series)
            appendSeriesLod(*series, key, result.segments, cancelled);
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
