#pragma once
#include "plotseriesstore.h"
#include <QJsonObject>
#include <QMap>
#include <atomic>

struct StateValue { QString text; QColor color; bool operator==(const StateValue &other) const { return text == other.text && color == other.color; } };
using SignalEnums = QMap<qint64, StateValue>;
QJsonArray signalEnumsToJson(const SignalEnums &values);
bool signalEnumsFromJson(const QJsonValue &json, SignalEnums *values);
struct StateLane {
    int source = -1;
    QString name;
    bool visible = true;
    QMap<qint64, StateValue> values;
};
struct StateTimelineConfig { bool enabled = false; QVector<StateLane> lanes; };
QJsonObject stateTimelineToJson(const StateTimelineConfig &config);
bool stateTimelineFromJson(const QJsonObject &json, int signalCount, StateTimelineConfig *config);

struct StateRun {
    double start = 0, end = 0, value = 0, raw = 0;
    bool known = false, leftOpen = false, rightOpen = false;
};
struct StateLaneData { StateLane config; QVector<StateRun> runs; QString error; };
struct StateTimelineData { QVector<StateLaneData> lanes; };
namespace StateTimeline {
std::optional<double> decode(double raw);
std::shared_ptr<const StateTimelineData> build(const StateTimelineConfig &config,
    const PlotSeriesSnapshot &snapshot, const std::atomic_bool &cancelled);
const StateRun *at(const StateLaneData &lane, double time);
QString text(const StateLane &config, const StateRun &run);
QColor color(const StateLane &config, const StateRun &run);
}
