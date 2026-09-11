#pragma once

#include <QColor>
#include <QPointF>
#include <QPen>
#include <QSet>
#include <QVector>

#include <memory>
#include <optional>

using PlotSeriesId = int;

struct PlotSeriesInput
{
    PlotSeriesId id = -1;
    QVector<double> time;
    QVector<double> values;
    QColor color;
    double lineWidth = 2.0;
    Qt::PenStyle lineStyle = Qt::SolidLine;
    QVector<QPointF> points;
    bool monotonicTime = true;
};

struct PlotSeriesData
{
    PlotSeriesId id = -1;
    QColor color;
    double lineWidth = 2.0;
    Qt::PenStyle lineStyle = Qt::SolidLine;
    QVector<QPointF> points;
    bool monotonicTime = true;
    quint64 version = 0;
};

using PlotSeriesDataPtr = std::shared_ptr<const PlotSeriesData>;

struct PlotSeriesSnapshot
{
    quint64 generation = 0;
    QVector<PlotSeriesId> orderedIds;
    QVector<PlotSeriesDataPtr> series;
};

struct PlotSample
{
    PlotSeriesId id = -1;
    double x = 0.0;
    double y = 0.0;
    QColor color;
};

struct PlotBounds
{
    double xMinimum = 0.0;
    double xMaximum = 0.0;
    double yMinimum = 0.0;
    double yMaximum = 0.0;
};

class PlotSeriesStore final
{
public:
    void replaceSeries(const QVector<PlotSeriesInput> &inputs);
    void appendSeries(const QVector<PlotSeriesInput> &inputs);
    void updateSeriesPen(PlotSeriesId id, const QColor &color,
                         double lineWidth, Qt::PenStyle lineStyle);
    void removeSeries(const QSet<PlotSeriesId> &ids);
    void clear();
    quint64 generation() const;
    PlotSeriesSnapshot snapshot(const QVector<PlotSeriesId> &orderedIds) const;

    static std::optional<double> nearestX(const PlotSeriesSnapshot &snapshot,
                                          double targetX);
    static QVector<PlotSample> nearestSamples(const PlotSeriesSnapshot &snapshot,
                                              double targetX);
    static std::optional<PlotBounds> bounds(const PlotSeriesSnapshot &snapshot);

private:
    quint64 m_generation = 0;
    QVector<PlotSeriesDataPtr> m_series;
};
