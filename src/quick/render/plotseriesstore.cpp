#include "plotseriesstore.h"

#include <QtMath>

#include <limits>
#include <utility>

namespace {

PlotSeriesDataPtr makeSeriesData(const PlotSeriesInput &input, quint64 version)
{
    auto data = std::make_shared<PlotSeriesData>();
    data->id = input.id;
    data->color = input.color.isValid() ? input.color : QColor("#4ea1ff");
    data->lineWidth = qBound(1.0, input.lineWidth, 20.0);
    data->lineStyle = input.lineStyle;
    data->version = version;
    data->sourceFile = input.sourceFile;
    data->sourceTable = input.sourceTable;
    data->sourceColumn = input.sourceColumn;
    data->sourceTableName = input.sourceTableName;
    data->timeOffset = input.timeOffset;

    if (!input.points.isEmpty()) {
        data->points = input.points;
    } else {
        data->time = input.time;
        data->values = input.values;
    }
    data->monotonicTime = input.monotonicTime;
    data->rangeIndex = input.rangeIndex;
    if (!data->rangeIndex || data->rangeIndex->sampleCount() != data->sampleCount())
        data->rangeIndex = PlotRangeIndex::build(*data);
    if (input.monotonicTimeKnown) return data;
    data->monotonicTime = true;
    double previousTime = -std::numeric_limits<double>::infinity();
    const qsizetype count = data->sampleCount();
    for (qsizetype index = 0; index < count; ++index) {
        const double timestamp = data->pointAt(index).x();
        if (!qIsFinite(timestamp) || timestamp < previousTime) {
            data->monotonicTime = false;
            break;
        }
        previousTime = timestamp;
    }
    return data;
}

// Preserve original-index tie breaking, including duplicate timestamps.
std::optional<QPointF> nearestPoint(const PlotSeriesData &series,
                                   double target, bool requireY)
{
    std::optional<QPointF> best;
    double distance = std::numeric_limits<double>::infinity();
    auto consider = [&](qsizetype index) {
        const QPointF point = series.pointAt(index);
        if (!qIsFinite(point.x()) || (requireY && !qIsFinite(point.y()))) return;
        const double delta = qAbs(point.x() - target);
        if (!best || delta < distance) { best = point; distance = delta; }
    };
    const qsizetype count = series.sampleCount();
    if (!series.monotonicTime) {
        for (qsizetype i = 0; i < count; ++i) consider(i);
        return best;
    }
    auto lowerBound = [&](double value) {
        qsizetype lo = 0, hi = count;
        while (lo < hi) {
            const qsizetype mid = lo + (hi - lo) / 2;
            if (series.pointAt(mid).x() < value) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    };
    const qsizetype split = lowerBound(target);
    qsizetype left = split - 1;
    while (left >= 0 && requireY && !qIsFinite(series.pointAt(left).y())) --left;
    if (left >= 0) {
        const double timestamp = series.pointAt(left).x();
        qsizetype first = lowerBound(timestamp);
        while (first < left && requireY && !qIsFinite(series.pointAt(first).y())) ++first;
        consider(first);
    }
    qsizetype right = split;
    while (right < count && requireY && !qIsFinite(series.pointAt(right).y())) ++right;
    if (right < count) consider(right);
    return best;
}

} // namespace

qsizetype PlotSeriesData::sampleCount() const
{
    return points.isEmpty() ? qMin(time.size(), values.size()) : points.size();
}

QPointF PlotSeriesData::pointAt(qsizetype index) const
{
    if (!points.isEmpty()) return {points.at(index).x() + timeOffset, points.at(index).y()};
    const double timestamp = time.at(index) + timeOffset;
    const double value = values.at(index);
    return {timestamp, qIsFinite(value) ? value : qQNaN()};
}

void PlotSeriesStore::replaceSeries(const QVector<PlotSeriesInput> &inputs)
{
    const quint64 nextGeneration = m_generation + 1;
    QVector<PlotSeriesDataPtr> replacement;
    replacement.reserve(inputs.size());
    for (const PlotSeriesInput &input : inputs)
        replacement.append(makeSeriesData(input, nextGeneration));

    m_series = std::move(replacement);
    rebuildPositions();
    m_generation = nextGeneration;
}

void PlotSeriesStore::appendSeries(const QVector<PlotSeriesInput> &inputs)
{
    if (inputs.isEmpty()) return;
    const quint64 nextGeneration = m_generation + 1;
    QVector<PlotSeriesDataPtr> appended = m_series;
    appended.reserve(m_series.size() + inputs.size());
    for (const PlotSeriesInput &input : inputs)
        appended.append(makeSeriesData(input, nextGeneration));
    m_series = std::move(appended);
    rebuildPositions();
    m_generation = nextGeneration;
}

void PlotSeriesStore::updateSeriesPen(PlotSeriesId id, const QColor &color,
                                      double lineWidth, Qt::PenStyle lineStyle)
{
    const auto position = m_positions.constFind(id);
    if (position == m_positions.cend()) return;
    const auto &current = m_series.at(*position);
    const QColor normalizedColor = color.isValid() ? color : QColor("#4ea1ff");
    const double normalizedWidth = qBound(1.0, lineWidth, 20.0);
    if (current->color == normalizedColor && current->lineWidth == normalizedWidth
        && current->lineStyle == lineStyle) return;
    auto updated = std::make_shared<PlotSeriesData>(*current);
    updated->color = normalizedColor;
    updated->lineWidth = normalizedWidth;
    updated->lineStyle = lineStyle;
    updated->version = m_generation + 1;
    m_series[*position] = std::move(updated);
    // IDs and their positions did not change; neither copy nor rebuild the map.
    ++m_generation;
}

bool PlotSeriesStore::addTimeOffset(const QSet<PlotSeriesId> &ids, double seconds, bool absolute)
{
    if (ids.isEmpty() || !qIsFinite(seconds)) return false;
    bool changed = false;
    // Validate the entire batch before replacing any immutable snapshots.
    for (const auto &series : m_series) {
        if (!ids.contains(series->id)) continue;
        const double offset = absolute ? seconds : series->timeOffset + seconds;
        if (!qIsFinite(offset)) return false;
        if (offset == series->timeOffset) continue;
        changed = true;
        auto unshifted = std::make_shared<PlotSeriesData>(*series);
        unshifted->timeOffset = 0;
        const auto limits = timeBounds({0, {}, {unshifted}});
        if (limits && (!qIsFinite(limits->first + offset)
                       || !qIsFinite(limits->second + offset))) return false;
    }
    if (!changed) return true;
    for (auto &series : m_series) {
        if (!ids.contains(series->id)) continue;
        const double offset = absolute ? seconds : series->timeOffset + seconds;
        if (offset == series->timeOffset) continue;
        auto updated = std::make_shared<PlotSeriesData>(*series);
        updated->timeOffset = offset;
        updated->version = m_generation + 1;
        series = std::move(updated);
    }
    ++m_generation;
    return true;
}

void PlotSeriesStore::removeSeries(const QSet<PlotSeriesId> &ids)
{
    if (ids.isEmpty()) return;
    const quint64 nextGeneration = m_generation + 1;
    QVector<PlotSeriesDataPtr> replacement;
    replacement.reserve(m_series.size());
    for (const PlotSeriesDataPtr &current : std::as_const(m_series)) {
        if (!current || ids.contains(current->id)) continue;
        auto updated = std::make_shared<PlotSeriesData>(*current);
        updated->id = replacement.size();
        updated->version = nextGeneration;
        replacement.append(std::move(updated));
    }
    if (replacement.size() == m_series.size()) return;
    m_series = std::move(replacement);
    rebuildPositions();
    m_generation = nextGeneration;
}

void PlotSeriesStore::rebuildPositions()
{
    m_positions.clear();
    m_positions.reserve(m_series.size());
    for (qsizetype position = 0; position < m_series.size(); ++position) {
        const auto &series = m_series.at(position);
        // Preserve legacy first-match semantics for duplicate IDs.
        if (series && !m_positions.contains(series->id))
            m_positions.insert(series->id, position);
    }
}

void PlotSeriesStore::clear()
{
    m_series.clear();
    m_positions.clear();
    ++m_generation;
}

quint64 PlotSeriesStore::generation() const
{
    return m_generation;
}

PlotSeriesSnapshot PlotSeriesStore::snapshot(const QVector<PlotSeriesId> &orderedIds) const
{
    PlotSeriesSnapshot result;
    result.generation = m_generation;
    result.orderedIds.reserve(orderedIds.size());
    result.series.reserve(orderedIds.size());
    for (PlotSeriesId id : orderedIds) {
        const auto position = m_positions.constFind(id);
        if (position == m_positions.cend()) continue;
        result.orderedIds.append(id);
        result.series.append(m_series.at(*position));
    }
    return result;
}

std::optional<double> PlotSeriesStore::nearestX(const PlotSeriesSnapshot &snapshot,
                                                double targetX)
{
    if (!qIsFinite(targetX))
        return std::nullopt;

    double nearest = 0.0;
    double bestDistance = std::numeric_limits<double>::max();
    bool found = false;
    for (const PlotSeriesDataPtr &series : snapshot.series) {
        const auto point = nearestPoint(*series, targetX, false);
        if (!point) continue;
        const double distance = qAbs(point->x() - targetX);
        if (!found || distance < bestDistance) {
            bestDistance = distance;
            nearest = point->x();
            found = true;
        }
    }
    return found ? std::optional<double>(nearest) : std::nullopt;
}

QVector<PlotSample> PlotSeriesStore::nearestSamples(const PlotSeriesSnapshot &snapshot,
                                                    double targetX)
{
    QVector<PlotSample> result;
    if (!qIsFinite(targetX))
        return result;

    result.reserve(snapshot.series.size());
    for (const PlotSeriesDataPtr &series : snapshot.series) {
        const auto point = nearestPoint(*series, targetX, true);
        if (point)
            result.append({series->id, point->x(), point->y(), series->color});
    }
    return result;
}

std::optional<PlotBounds> PlotSeriesStore::bounds(const PlotSeriesSnapshot &snapshot,
                                                  double xMinimum, double xMaximum,
                                                  PlotBoundsQueryStats *stats)
{
    std::optional<PlotBounds> result;
    for (const auto &series : snapshot.series) {
        // Legacy hand-built snapshots may not have an index yet. Store snapshots do.
        const auto index = series->rangeIndex ? series->rangeIndex : PlotRangeIndex::build(*series);
        const auto bounds = index->bounds(*series, xMinimum, xMaximum, stats);
        if (!bounds) continue;
        if (!result) { result = bounds; continue; }
        result->xMinimum = qMin(result->xMinimum, bounds->xMinimum);
        result->xMaximum = qMax(result->xMaximum, bounds->xMaximum);
        result->yMinimum = qMin(result->yMinimum, bounds->yMinimum);
        result->yMaximum = qMax(result->yMaximum, bounds->yMaximum);
    }
    return result;
}

std::optional<QPair<double, double>> PlotSeriesStore::timeBounds(
    const PlotSeriesSnapshot &snapshot, PlotBoundsQueryStats *stats)
{
    std::optional<QPair<double, double>> result;
    auto add = [&](double x) {
        if (!qIsFinite(x)) return;
        if (!result) result = qMakePair(x, x);
        else { result->first = qMin(result->first, x); result->second = qMax(result->second, x); }
    };
    for (const auto &series : snapshot.series) {
        if (!series || series->sampleCount() == 0) continue;
        bool mustScan = false;
        if (series->rangeIndex && series->rangeIndex->sampleCount() == series->sampleCount()) {
            if (stats) ++stats->indexNodes;
            const auto raw = series->rangeIndex->rawTimeBounds();
            if (!raw) continue;
            const double lo = raw->first + series->timeOffset;
            const double hi = raw->second + series->timeOffset;
            if (qIsFinite(lo) && qIsFinite(hi)) { add(lo); add(hi); continue; }
            mustScan = true;
            // Hand-built snapshots can contain overflowing offsets. Fall back to
            // the legacy finite-sample scan rather than hiding an interior value.
        }
        if (series->monotonicTime && !mustScan) {
            if (stats) stats->rawSamples += 2;
            add(series->pointAt(0).x());
            add(series->pointAt(series->sampleCount() - 1).x());
        } else {
            for (qsizetype i = 0; i < series->sampleCount(); ++i) {
                if (stats) ++stats->rawSamples;
                add(series->pointAt(i).x());
            }
        }
    }
    return result;
}
