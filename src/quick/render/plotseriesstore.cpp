#include "plotseriesstore.h"

#include <QtMath>

#include <limits>

void PlotSeriesStore::replaceSeries(const QVector<PlotSeriesInput> &inputs)
{
    const quint64 nextGeneration = m_generation + 1;
    QVector<PlotSeriesDataPtr> replacement;
    replacement.reserve(inputs.size());
    for (const PlotSeriesInput &input : inputs) {
        auto data = std::make_shared<PlotSeriesData>();
        data->id = input.id;
        data->color = input.color.isValid() ? input.color : QColor("#4ea1ff");
        data->lineWidth = qBound(1.0, input.lineWidth, 20.0);
        data->lineStyle = input.lineStyle;
        data->version = nextGeneration;

        const int count = qMin(input.time.size(), input.values.size());
        data->points.reserve(count);
        double previousTime = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < count; ++i) {
            const double timestamp = input.time.at(i);
            const double value = input.values.at(i);
            if (!qIsFinite(timestamp)) {
                data->monotonicTime = false;
                data->points.append(QPointF(qQNaN(), qQNaN()));
                continue;
            }
            if (timestamp < previousTime)
                data->monotonicTime = false;
            previousTime = timestamp;
            data->points.append(QPointF(timestamp,
                                        qIsFinite(value) ? value : qQNaN()));
        }
        replacement.append(std::move(data));
    }

    m_series = std::move(replacement);
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
        for (const QPointF &point : series->points) {
            if (!qIsFinite(point.x()))
                continue;
            const double distance = qAbs(point.x() - targetX);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = point.x();
                found = true;
            }
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
        double bestDistance = std::numeric_limits<double>::max();
        std::optional<PlotSample> nearest;
        for (const QPointF &point : series->points) {
            if (!qIsFinite(point.x()) || !qIsFinite(point.y()))
                continue;
            const double distance = qAbs(point.x() - targetX);
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = PlotSample{series->id, point.x(), point.y(), series->color};
            }
        }
        if (nearest.has_value())
            result.append(*nearest);
    }
    return result;
}

std::optional<PlotBounds> PlotSeriesStore::bounds(const PlotSeriesSnapshot &snapshot)
{
    PlotBounds result;
    bool found = false;
    for (const PlotSeriesDataPtr &series : snapshot.series) {
        for (const QPointF &point : series->points) {
            if (!qIsFinite(point.x()) || !qIsFinite(point.y()))
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
