#pragma once
#include "render/plotseriesstore.h"
#include <atomic>
#include <map>
#include <tuple>

// Per-evaluation bounded cache. Identities refer to retained immutable arrays.
class MergeTimelineCache {
public:
    struct Entry { int source; qsizetype index; double time; };
    struct Timeline { QVector<Entry> order; QVector<double> time; QVector<qsizetype> starts; };
    std::shared_ptr<const Timeline> get(const QVector<PlotSeriesDataPtr> &inputs,
        const std::atomic_bool &cancelled, QString *error);
    qsizetype hits = 0, builds = 0, bytes = 0;
private:
    using Key = std::vector<std::tuple<quintptr, qsizetype, double, bool>>;
    std::map<Key, std::shared_ptr<const Timeline>> m_cache;
};
