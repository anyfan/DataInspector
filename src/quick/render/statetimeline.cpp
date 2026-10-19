#include "statetimeline.h"
#include <QJsonArray>
#include <cmath>
#include <algorithm>

QJsonArray signalEnumsToJson(const SignalEnums &values)
{
    QJsonArray result;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        result.append(QJsonObject{{"value", double(it.key())}, {"text", it->text}, {"color", it->color.name(QColor::HexArgb)}});
    return result;
}
bool signalEnumsFromJson(const QJsonValue &json, SignalEnums *values)
{
    if (!json.isArray() || json.toArray().size() > 256) return false;
    SignalEnums parsed;
    for (const auto &v : json.toArray()) {
        const auto o = v.toObject();
        if (!v.isObject() || o.keys() != QStringList{"color", "text", "value"}
            || !o["value"].isDouble() || !std::isfinite(o["value"].toDouble())
            || std::floor(o["value"].toDouble()) != o["value"].toDouble() || std::abs(o["value"].toDouble()) > 9007199254740991.
            || !o["text"].isString() || o["text"].toString().trimmed().isEmpty() || o["text"].toString().size() > 256
            || o["text"].toString().contains(QChar::Null) || o["text"].toString().contains('\n') || o["text"].toString().contains('\r')
            || !o["color"].isString() || !QColor(o["color"].toString()).isValid()) return false;
        const auto key = qint64(o["value"].toDouble());
        if (parsed.contains(key)) return false;
        parsed.insert(key, {o["text"].toString(), QColor(o["color"].toString())});
    }
    *values = parsed; return true;
}
QJsonObject stateTimelineToJson(const StateTimelineConfig &config)
{
    QJsonArray lanes;
    for (const auto &lane : config.lanes)
        lanes.append(QJsonObject{{"source", lane.source}, {"name", lane.name}, {"visible", lane.visible}});
    return {{"enabled", config.enabled}, {"lanes", lanes}};
}
bool stateTimelineFromJson(const QJsonObject &json, int count, StateTimelineConfig *config)
{
    if (json.keys() != QStringList{"enabled", "lanes"} || !json["enabled"].isBool() || !json["lanes"].isArray() || json["lanes"].toArray().size() > 64) return false;
    StateTimelineConfig parsed; parsed.enabled = json["enabled"].toBool();
    for (const auto &v : json["lanes"].toArray()) {
        const auto o = v.toObject(); StateLane lane;
        const double source = o["source"].toDouble(-1);
        if (!v.isObject() || o.keys() != QStringList{"name", "source", "visible"} || !o["source"].isDouble()
            || !std::isfinite(source) || std::floor(source) != source || source < 0 || source >= count
            || !o["name"].isString() || o["name"].toString().trimmed().isEmpty() || o["name"].toString().size() > 256
            || o["name"].toString().contains(QChar::Null) || !o["visible"].isBool()) return false;
        lane.source = int(source); lane.name = o["name"].toString(); lane.visible = o["visible"].toBool();
        parsed.lanes.append(lane);
    }
    *config = parsed; return true;
}
std::optional<double> StateTimeline::decode(double raw)
{
    if (!std::isfinite(raw) || std::floor(raw) != raw || std::abs(raw) > 9007199254740991.) return {};
    return raw;
}

std::shared_ptr<const StateTimelineData> StateTimeline::build(const StateTimelineConfig &config,
    const PlotSeriesSnapshot &snapshot, const std::atomic_bool &cancelled)
{
    auto result = std::make_shared<StateTimelineData>();
    QHash<int, PlotSeriesDataPtr> sources; for (const auto &s : snapshot.series) sources.insert(s->id, s);
    qsizetype total = 0;
    for (const auto &lane : config.lanes) {
        if (!lane.visible) continue;
        StateLaneData row; row.config = lane;
        const auto source = sources.value(lane.source);
        if (!source) { row.error = QStringLiteral("来源尚未就绪"); result->lanes.append(row); continue; }
        double previous = -std::numeric_limits<double>::infinity();
        for (qsizetype i = 0; i < source->sampleCount(); ++i) {
            if ((i & 4095) == 0 && cancelled.load()) return {};
            const auto point = source->pointAt(i); const double time = point.x(), raw = point.y();
            // Reject ambiguous order/duplicate timestamps instead of inventing durations.
            if (!std::isfinite(time) || time <= previous) { row.error = QStringLiteral("时间须有限且严格递增（重复或乱序时间不支持状态区间）"); break; }
            previous = time;
            const auto decoded = decode(raw);
            const bool known = decoded.has_value();
            const double value = decoded.value_or(raw);
            if (!row.runs.isEmpty()) row.runs.last().end = time;
            if (row.runs.isEmpty() || row.runs.last().known != known || (known && row.runs.last().value != value)) {
                if (++total > 2000000) { row.error = QStringLiteral("状态区间超过总预算 200 万，请减少状态行或数据范围"); break; }
                row.runs.append({time, time, value, raw, known, i == 0, true});
                if (row.runs.size() > 1) row.runs[row.runs.size()-2].rightOpen = false;
            }
        }
        if (!row.error.isEmpty()) row.runs.clear();
        result->lanes.append(std::move(row));
    }
    return result;
}
const StateRun *StateTimeline::at(const StateLaneData &lane, double time)
{
    const auto &runs = lane.runs;
    auto it = std::upper_bound(runs.cbegin(), runs.cend(), time, [](double t, const StateRun &r) { return t < r.start; });
    if (it == runs.cbegin()) return nullptr;
    --it;
    return time <= it->end ? &*it : nullptr;
}
QString StateTimeline::text(const StateLane &config, const StateRun &run)
{
    if (!run.known) return QStringLiteral("未知");
    const auto it = config.values.constFind(qint64(run.value));
    return it == config.values.cend() ? QStringLiteral("未定义(%1)").arg(QString::number(run.value, 'f', 0)) : it->text;
}
QColor StateTimeline::color(const StateLane &config, const StateRun &run)
{
    if (!run.known) return QColor("#687887");
    const auto it = config.values.constFind(qint64(run.value));
    return it == config.values.cend() ? QColor("#a66d22") : it->color;
}
