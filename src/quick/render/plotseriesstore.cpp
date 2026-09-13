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

    if (!input.points.isEmpty()) {
        data->points = input.points;
        data->monotonicTime = input.monotonicTime;
        return data;
    }

    data->time = input.time;
    data->values = input.values;
    const int count = qMin(data->time.size(), data->values.size());
    data->monotonicTime = input.monotonicTime;
    if (input.monotonicTimeKnown) return data;
    data->monotonicTime = true;
    double previousTime = -std::numeric_limits<double>::infinity();
    for (int index = 0; index < count; ++index) {
        const double timestamp = data->time.at(index);
        if (!qIsFinite(timestamp)) {
            data->monotonicTime = false;
            continue;
        }
        if (timestamp < previousTime)
            data->monotonicTime = false;
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
    if (!points.isEmpty()) return points.at(index);
    const double timestamp = time.at(index);
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
    m_generation = nextGeneration;
}

void PlotSeriesStore::updateSeriesPen(PlotSeriesId id, const QColor &color,
                                      double lineWidth, Qt::PenStyle lineStyle)
{
    const quint64 nextGeneration = m_generation + 1;
    bool changed = false;
    QVector<PlotSeriesDataPtr> replacement = m_series;
    for (int index = 0; index < replacement.size(); ++index) {
        const PlotSeriesDataPtr &current = replacement.at(index);
        if (!current || current->id != id) continue;
        auto updated = std::make_shared<PlotSeriesData>(*current);
        updated->color = color.isValid() ? color : QColor("#4ea1ff");
        updated->lineWidth = qBound(1.0, lineWidth, 20.0);
        updated->lineStyle = lineStyle;
        updated->version = nextGeneration;
        replacement[index] = std::move(updated);
        changed = true;
        break;
    }
    if (!changed) return;
    m_series = std::move(replacement);
    m_generation = nextGeneration;
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
    m_generation = nextGeneration;
}

void PlotSeriesStore::clear()
{
    m_series.clear();
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
        for (const PlotSeriesDataPtr &series : m_series) {
            if (series->id != id)
                continue;
            result.orderedIds.append(id);
            result.series.append(series);
            break;
        }
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
                                                  double xMinimum, double xMaximum)
{
    PlotBounds result;
    bool found = false;
    for (const PlotSeriesDataPtr &series : snapshot.series) {
        qsizetype first = 0, last = series->sampleCount();
        if (series->monotonicTime) {
            auto boundary = [&](double value, bool upper) {
                qsizetype lo = 0, hi = series->sampleCount();
                while (lo < hi) {
                    const qsizetype mid = lo + (hi - lo) / 2;
                    const double x = series->pointAt(mid).x();
                    if (x < value || (upper && x == value)) lo = mid + 1;
                    else hi = mid;
                }
                return lo;
            };
            first = boundary(xMinimum, false);
            last = boundary(xMaximum, true);
        }
        for (qsizetype index = first; index < last; ++index) {
            const QPointF point = series->pointAt(index);
            if (!qIsFinite(point.x()) || !qIsFinite(point.y())
                || point.x() < xMinimum || point.x() > xMaximum)
                continue;
            if (!found) {
                result.xMinimum = result.xMaximum = point.x();
                result.yMinimum = result.yMaximum = point.y();
                found = true;
                continue;
            }
            result.xMinimum = qMin(result.xMinimum, point.x());
            result.xMaximum = qMax(result.xMaximum, point.x());
            result.yMinimum = qMin(result.yMinimum, point.y());
            result.yMaximum = qMax(result.yMaximum, point.y());
        }
    }
    return found ? std::optional<PlotBounds>(result) : std::nullopt;
}

std::optional<QPair<double, double>> PlotSeriesStore::timeBounds(const PlotSeriesSnapshot &snapshot)
{
    std::optional<QPair<double, double>> result;
    auto add = [&](double x) {
        if (!qIsFinite(x)) return;
        if (!result) result = qMakePair(x, x);
        else { result->first = qMin(result->first, x); result->second = qMax(result->second, x); }
    };
    for (const auto &series : snapshot.series) {
        if (series->sampleCount() == 0) continue;
        if (series->monotonicTime) {
            add(series->pointAt(0).x());
            add(series->pointAt(series->sampleCount() - 1).x());
        } else {
            for (qsizetype i = 0; i < series->sampleCount(); ++i) add(series->pointAt(i).x());
        }
    }
    return result;
}
