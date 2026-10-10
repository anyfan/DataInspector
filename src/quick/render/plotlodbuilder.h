#pragma once

#include "plotseriesstore.h"

#include <optional>
#include <atomic>

struct LodRequestKey
{
    quint64 storeGeneration = 0;
    QVector<PlotSeriesId> orderedIds;
    double xMinimum = 0.0;
    double xMaximum = 1.0;
    int bucketCount = 0;
    quint32 algorithmVersion = 3;

    bool operator==(const LodRequestKey &other) const;
};

struct LodDenseBucket
{
    double firstX = 0.0;
    double lastX = 0.0;
    qsizetype firstPoint = 0;
    double minimumLineWidth = 1.0; // In LOD bucket widths, checked after projection.
};

struct LodSegment
{
    PlotSeriesId seriesId = -1;
    QColor color;
    QVector<QPointF> points;
    double lineWidth = 0.0;
    Qt::PenStyle lineStyle = Qt::SolidLine;
    QVector<LodDenseBucket> denseBuckets;
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
                           const LodRequestKey &key,
                           const std::atomic_bool *cancelled = nullptr);
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

// GUI-owned LRU of immutable completed results. Weak source identities avoid
// retaining raw data after a file is removed or a session is replaced.
class PlotLodResultCache final
{
public:
    explicit PlotLodResultCache(qsizetype budget = 64 * 1024 * 1024) : m_budget(qMax(qsizetype(0), budget)) {}
    std::shared_ptr<const LodResult> find(const PlotSeriesSnapshot &snapshot, const LodRequestKey &key);
    void insert(const PlotSeriesSnapshot &snapshot, std::shared_ptr<const LodResult> result);
    qsizetype retainedBytes() const { return m_bytes; }
    qsizetype entryCount() const { return m_entries.size(); }
    void clear() { m_entries.clear(); m_bytes = 0; }
private:
    struct Entry {
        std::shared_ptr<const LodResult> result;
        QVector<std::weak_ptr<const PlotSeriesData>> sources;
        qsizetype bytes = 0;
    };
    void prune();
    QVector<Entry> m_entries; // Most recently used first; at most 128 entries.
    qsizetype m_budget;
    qsizetype m_bytes = 0;
};
