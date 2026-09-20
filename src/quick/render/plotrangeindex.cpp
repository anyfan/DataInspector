#include "plotrangeindex.h"
#include "plotseriesstore.h"
#include <QtMath>

void PlotRangeIndex::merge(Node &into, const Node &other)
{
    if (!other.valid) return;
    if (!into.valid) { into = other; return; }
    into.bounds.xMinimum = qMin(into.bounds.xMinimum, other.bounds.xMinimum);
    into.bounds.xMaximum = qMax(into.bounds.xMaximum, other.bounds.xMaximum);
    into.bounds.yMinimum = qMin(into.bounds.yMinimum, other.bounds.yMinimum);
    into.bounds.yMaximum = qMax(into.bounds.yMaximum, other.bounds.yMaximum);
}

PlotRangeIndexPtr PlotRangeIndex::build(const PlotSeriesData &series,
                                      const std::function<bool()> &isCancelled)
{
    auto index = std::make_shared<PlotRangeIndex>();
    index->m_count = series.sampleCount();
    const qsizetype blocks = index->m_count / blockSize + (index->m_count % blockSize != 0);
    while (index->m_leaves < blocks) index->m_leaves *= 2;
    index->m_tree.resize(index->m_leaves * 2);
    for (qsizetype row = 0; row < index->m_count; ++row) {
        if ((row & 0xfff) == 0 && isCancelled && isCancelled()) return {};
        // Store unshifted X, so offsets never rebuild the index.
        const double x = series.points.isEmpty() ? series.time.at(row) : series.points.at(row).x();
        const double y = series.points.isEmpty() ? series.values.at(row) : series.points.at(row).y();
        if (!qIsFinite(x) || !qIsFinite(y)) continue;
        merge(index->m_tree[index->m_leaves + row / blockSize], {{x, x, y, y}, true});
    }
    for (qsizetype node = index->m_leaves - 1; node > 0; --node) {
        if ((node & 0xfff) == 0 && isCancelled && isCancelled()) return {};
        Node &value = index->m_tree[node];
        merge(value, index->m_tree.at(node * 2));
        merge(value, index->m_tree.at(node * 2 + 1));
    }
    return index;
}

PlotRangeIndex::Node PlotRangeIndex::queryBlocks(qsizetype first, qsizetype last,
                                                PlotBoundsQueryStats *stats) const
{
    Node result;
    for (first += m_leaves, last += m_leaves; first < last; first /= 2, last /= 2) {
        if (first & 1) { merge(result, m_tree.at(first++)); if (stats) ++stats->indexNodes; }
        if (last & 1) { merge(result, m_tree.at(--last)); if (stats) ++stats->indexNodes; }
    }
    return result;
}

std::optional<PlotBounds> PlotRangeIndex::bounds(const PlotSeriesData &series,
    double xMinimum, double xMaximum, PlotBoundsQueryStats *stats) const
{
    if (qIsNaN(xMinimum) || qIsNaN(xMaximum) || xMinimum > xMaximum) return {};
    Node result;
    auto scan = [&](qsizetype first, qsizetype last) {
        for (qsizetype row = first; row < last; ++row) {
            if (stats) ++stats->rawSamples;
            const QPointF point = series.pointAt(row);
            if (!qIsFinite(point.x()) || !qIsFinite(point.y())
                || point.x() < xMinimum || point.x() > xMaximum) continue;
            merge(result, {{point.x(), point.x(), point.y(), point.y()}, true});
        }
    };
    auto shifted = [&](Node node) {
        node.bounds.xMinimum += series.timeOffset;
        node.bounds.xMaximum += series.timeOffset;
        return node;
    };
    if (series.monotonicTime) {
        auto boundary = [&](double x, bool upper) {
            qsizetype lo = 0, hi = m_count;
            while (lo < hi) {
                const qsizetype mid = lo + (hi - lo) / 2;
                const double value = series.pointAt(mid).x();
                if (value < x || (upper && value == x)) lo = mid + 1;
                else hi = mid;
            }
            return lo;
        };
        const qsizetype first = boundary(xMinimum, false);
        const qsizetype last = boundary(xMaximum, true);
        const qsizetype blockFirst = first / blockSize + (first % blockSize != 0);
        const qsizetype blockLast = last / blockSize;
        if (blockFirst >= blockLast) {
            scan(first, last);
        } else {
            scan(first, blockFirst * blockSize);
            merge(result, shifted(queryBlocks(blockFirst, blockLast, stats)));
            scan(blockLast * blockSize, last);
        }
    } else {
        // Unordered times cannot use binary search. Whole blocks still skip/merge
        // cheaply; only blocks straddling the requested time window are scanned.
        const qsizetype blocks = m_count / blockSize + (m_count % blockSize != 0);
        for (qsizetype block = 0; block < blocks; ++block) {
            if (stats) ++stats->indexNodes;
            const Node node = shifted(m_tree.at(m_leaves + block));
            if (!node.valid || node.bounds.xMaximum < xMinimum || node.bounds.xMinimum > xMaximum)
                continue;
            if (node.bounds.xMinimum >= xMinimum && node.bounds.xMaximum <= xMaximum)
                merge(result, node);
            else
                scan(block * blockSize, qMin(m_count, (block + 1) * blockSize));
        }
    }
    return result.valid ? std::optional<PlotBounds>(result.bounds) : std::nullopt;
}
