#include "plotlodbuilder.h"

void PlotLodResultCache::prune()
{
    for (qsizetype i = m_entries.size(); i-- > 0;) {
        bool expired = false;
        for (const auto &source : m_entries[i].sources) if (source.expired()) { expired = true; break; }
        if (expired) { m_bytes -= m_entries[i].bytes; m_entries.removeAt(i); }
    }
}

std::shared_ptr<const LodResult> PlotLodResultCache::find(const PlotSeriesSnapshot &snapshot, const LodRequestKey &key)
{
    prune();
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const auto &entry = m_entries[i];
        if (!(entry.result->key == key) || entry.sources.size() != snapshot.series.size()) continue;
        bool matches = true;
        for (qsizetype j = 0; j < entry.sources.size(); ++j)
            if (entry.sources[j].lock() != snapshot.series[j]) { matches = false; break; }
        if (!matches) continue;
        const auto result = entry.result;
        m_entries.move(i, 0);
        return result;
    }
    return {};
}

void PlotLodResultCache::insert(const PlotSeriesSnapshot &snapshot, std::shared_ptr<const LodResult> result)
{
    prune();
    if (!result || find(snapshot, result->key)) return;
    Entry entry;
    entry.result = std::move(result);
    for (const auto &source : snapshot.series) entry.sources.append(source);
    entry.bytes = sizeof(Entry) + sizeof(LodResult)
        + entry.sources.capacity() * sizeof(std::weak_ptr<const PlotSeriesData>)
        + entry.result->key.orderedIds.capacity() * sizeof(PlotSeriesId)
        + entry.result->segments.capacity() * sizeof(LodSegment);
    for (const auto &segment : entry.result->segments)
        entry.bytes += segment.points.capacity() * sizeof(QPointF)
            + segment.denseBuckets.capacity() * sizeof(LodDenseBucket);
    if (entry.bytes > m_budget) return;
    while (!m_entries.isEmpty() && (m_bytes + entry.bytes > m_budget || m_entries.size() >= 128)) {
        m_bytes -= m_entries.last().bytes;
        m_entries.removeLast();
    }
    m_bytes += entry.bytes;
    m_entries.prepend(std::move(entry));
}

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
    double firstX = 0.0, lastX = 0.0;
    double lastY = 0.0;
    double totalVariation = 0.0;
    int direction = 0, reversals = 0;
    int firstDirection = 0;
    double firstY = 0;
    struct BucketSummary {
        LodDenseBucket bucket;
        double minimum, maximum, firstY, lastY, variation;
        qsizetype samples;
        int firstDirection, lastDirection, reversals;
    };
    QVector<BucketSummary> buckets;

    const double span = key.xMaximum - key.xMinimum;
    auto flushBucket = [&]() {
        if (minimum.has_value()) {
            if (series.monotonicTime)
                buckets.append({{firstX, lastX, current.points.size()},
                                minimum->point.y(), maximum->point.y(), firstY, lastY,
                                totalVariation, bucketSamples, firstDirection, direction, reversals});
            appendReducedBucket(current.points, *minimum, *maximum);
        }
        bucketSamples = 0;
        direction = 0;
        firstDirection = 0;
        reversals = 0;
        totalVariation = 0.0;
        minimum.reset();
        maximum.reset();
    };
    auto flushSegment = [&]() {
        flushBucket();
        // A cycle can straddle pixel buckets at intermediate zoom. Classify
        // oscillation at several bounded neighborhood sizes, then supplement
        // each bucket with its own extrema (never spread neighboring peaks).
        for (qsizetype i = 0; i < buckets.size(); ++i) {
            if ((i & 1023) == 0 && cancelled && cancelled->load()) return;
            const auto &candidate = buckets[i];
            if (candidate.minimum == candidate.maximum
                || candidate.bucket.firstX < key.xMinimum
                || candidate.bucket.lastX > key.xMaximum)
                continue;
            for (int radius : {2, 4, 8, 16, 32}) {
                double low = candidate.minimum, high = candidate.maximum, travel = 0;
                qsizetype samples = 0;
                int turns = 0, previousDirection = 0;
                double previousY = 0;
                for (qsizetype j = qMax(qsizetype(0), i - radius);
                     j < qMin(buckets.size(), i + radius + 1); ++j) {
                    const auto &bucket = buckets[j];
                    low = qMin(low, bucket.minimum); high = qMax(high, bucket.maximum);
                    if (samples > 0 && bucket.firstY != previousY) {
                        const int bridgeDirection = bucket.firstY > previousY ? 1 : -1;
                        if (previousDirection && previousDirection != bridgeDirection) ++turns;
                        previousDirection = bridgeDirection;
                        travel += qAbs(bucket.firstY - previousY);
                    }
                    if (bucket.firstDirection) {
                        if (previousDirection && previousDirection != bucket.firstDirection) ++turns;
                        previousDirection = bucket.lastDirection;
                    }
                    turns += bucket.reversals;
                    travel += bucket.variation;
                    samples += bucket.samples;
                    previousY = bucket.lastY;
                }
                const double amplitude = high - low;
                if (samples >= 4 && turns >= 2 && amplitude > 0
                    && qIsFinite(amplitude) && qIsFinite(travel) && travel / amplitude >= 3) {
                    auto denseBucket = candidate.bucket;
                    denseBucket.minimumLineWidth = radius * .5;
                    current.denseBuckets.append(denseBucket);
                    break;
                }
            }
        }
        buckets.clear();
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
        if (bucketSamples > 0 && point.y() != lastY) {
            totalVariation += qAbs(point.y() - lastY);
            const int nextDirection = point.y() > lastY ? 1 : -1;
            if (firstDirection == 0) firstDirection = nextDirection;
            if (direction != 0 && direction != nextDirection) ++reversals;
            direction = nextDirection;
        }
        lastY = point.y();
        if (bucketSamples++ == 0) { firstX = point.x(); firstY = point.y(); }
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
