#pragma once

#include "plotseriesstore.h"

#include <optional>

struct LodRequestKey
{
    quint64 storeGeneration = 0;
    QVector<PlotSeriesId> orderedIds;
    double xMinimum = 0.0;
    double xMaximum = 1.0;
    int bucketCount = 0;
    quint32 algorithmVersion = 1;

    bool operator==(const LodRequestKey &other) const;
};

struct LodSegment
{
    PlotSeriesId seriesId = -1;
    QColor color;
    QVector<QPointF> points;
    double lineWidth = 0.0;
    Qt::PenStyle lineStyle = Qt::SolidLine;
};

struct LodResult
{
    LodRequestKey key;
    QVector<LodSegment> segments;
};

class PlotLodBuilder final
{
public:
    static LodResult build(const PlotSeriesSnapshot &snapshot,
                           const LodRequestKey &key);
};

class PlotLodCache final
{
public:
    const LodResult &resolve(const PlotSeriesSnapshot &snapshot,
                             const LodRequestKey &key);
    void clear();
    quint64 rebuildCount() const { return m_rebuildCount; }

private:
    std::optional<LodResult> m_result;
    quint64 m_rebuildCount = 0;
};
