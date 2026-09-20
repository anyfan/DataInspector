#pragma once

#include <QVector>
#include <functional>
#include <memory>
#include <optional>

struct PlotSeriesData;
struct PlotBounds
{
    double xMinimum = 0.0;
    double xMaximum = 0.0;
    double yMinimum = 0.0;
    double yMaximum = 0.0;
};

// Optional counters for deterministic performance tests (not wall-clock assertions).
struct PlotBoundsQueryStats
{
    qsizetype rawSamples = 0;
    qsizetype indexNodes = 0;
};

// Immutable, raw-time block summaries and a segment tree over those summaries.
// The loader builds this off the GUI thread; pen/offset snapshots share it.
class PlotRangeIndex final
{
public:
    static constexpr qsizetype blockSize = 512;
    static std::shared_ptr<const PlotRangeIndex> build(
        const PlotSeriesData &series, const std::function<bool()> &isCancelled = {});
    std::optional<PlotBounds> bounds(const PlotSeriesData &series, double xMinimum,
                                    double xMaximum, PlotBoundsQueryStats *stats) const;
    qsizetype sampleCount() const { return m_count; }
    qsizetype storageBytes() const { return m_tree.size() * sizeof(Node); }

private:
    struct Node { PlotBounds bounds; bool valid = false; };
    static void merge(Node &into, const Node &other);
    Node queryBlocks(qsizetype first, qsizetype last, PlotBoundsQueryStats *stats) const;
    qsizetype m_count = 0;
    qsizetype m_leaves = 1;
    QVector<Node> m_tree;
};
using PlotRangeIndexPtr = std::shared_ptr<const PlotRangeIndex>;
