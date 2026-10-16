#include "mergetimeline.h"
#include <algorithm>
#include <cmath>

std::shared_ptr<const MergeTimelineCache::Timeline> MergeTimelineCache::get(
    const QVector<PlotSeriesDataPtr> &inputs, const std::atomic_bool &cancelled, QString *error, bool reuse)
{
    Key key;
    qsizetype count = 0;
    for (const auto &s : inputs) {
        if (!s || !std::isfinite(s->timeOffset)) { *error = QStringLiteral("合并来源含无效时间戳"); return {}; }
        key.emplace_back(quintptr(s->points.isEmpty() ? static_cast<const void *>(s->time.constData())
            : static_cast<const void *>(s->points.constData())), s->sampleCount(), s->timeOffset, !s->points.isEmpty());
        count += s->sampleCount();
    }
    if (reuse) {
        const auto it = m_cache.find(key);
        if (it != m_cache.end()) { ++hits; return it->second; }
    }
    auto timeline = std::make_shared<Timeline>(); timeline->order.reserve(count);
    for (int source = 0; source < inputs.size(); ++source)
        for (qsizetype index = 0; index < inputs[source]->sampleCount(); ++index) {
            if ((index & 4095) == 0 && cancelled.load()) return {};
            const double time = inputs[source]->pointAt(index).x();
            if (!std::isfinite(time)) { *error = QStringLiteral("合并来源含无效时间戳"); return {}; }
            timeline->order.append({source, index, time});
        }
    std::sort(timeline->order.begin(), timeline->order.end(), [](const Entry &a, const Entry &b) { return a.time < b.time; });
    for (qsizetype i = 0; i < count; ++i) {
        if ((i & 4095) == 0 && cancelled.load()) return {};
        const double time = timeline->order[i].time;
        if (timeline->time.isEmpty() || timeline->time.last() != time) {
            timeline->time.append(time); timeline->starts.append(i);
        }
    }
    timeline->starts.append(count); ++builds;
    const qsizetype cost = timeline->order.capacity() * qsizetype(sizeof(Entry))
        + timeline->time.capacity() * qsizetype(sizeof(double)) + timeline->starts.capacity() * qsizetype(sizeof(qsizetype));
    if (reuse && cost <= 64 * 1024 * 1024 - bytes) { m_cache.emplace(std::move(key), timeline); bytes += cost; }
    return timeline;
}
