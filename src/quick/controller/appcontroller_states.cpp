#include "appcontroller.h"
#include "items/statetimelineitem.h"
#include <QJsonDocument>
#include <cmath>

bool AppController::configureSignalProperties(int row, const QVariantMap &properties)
{
    if (sessionInteractionBlocked() || m_loading || m_exporting || row < 0 || row >= signalCount()) return false;
    SignalEnums enums;
    const QString name = properties.value("name").toString().trimmed();
    const QColor color(properties.value("color").toString());
    bool widthOk=false, styleOk=false, offsetOk=false;
    const double width=properties.value("width").toDouble(&widthOk), style=properties.value("style").toDouble(&styleOk);
    const auto offset=properties.value("timeOffset");
    const double seconds=offset.toDouble(&offsetOk);
    const bool derived=isObjectOutput(row);
    if (name.isEmpty() || name.size()>256 || name.contains(QChar::Null) || !color.isValid()
        || !widthOk || !std::isfinite(width) || width<1 || width>20
        || !styleOk || !std::isfinite(style) || std::floor(style)!=style || style<1 || style>5
        || !signalEnumsFromJson(QJsonArray::fromVariantList(properties.value("enums").toList()), &enums)
        || (!derived && (!offsetOk || !std::isfinite(seconds))) || (derived && offset.isValid() && !offset.isNull())) {
        setStatus(QStringLiteral("属性无效：检查名称、样式、枚举和时间偏移；派生信号时间随输入来源")); return false;
    }
    // Validate the entire draft before any mutation; Store rejects time overflow atomically.
    if (!derived && seconds != signalTimeOffset(row) && !setTimeOffset(0,row,{},seconds)) return false;
    setSignalEnums(row,properties.value("enums").toList());
    renameSignal(row,name); setSignalPen(row,color,width,int(style));
    return true;
}
QVariantList AppController::signalEnums(int row) const
{
    return signalEnumsToJson(m_signalEnums.value(row)).toVariantList();
}
bool AppController::setSignalEnums(int row, const QVariantList &values)
{
    if (sessionInteractionBlocked() || m_loading || m_exporting || row < 0 || row >= signalCount()) return false;
    SignalEnums parsed;
    if (!signalEnumsFromJson(QJsonArray::fromVariantList(values), &parsed)) {
        setStatus(QStringLiteral("枚举定义无效：数值须为唯一的安全整数，名称非空，颜色有效，最多 256 项")); return false;
    }
    if (m_signalEnums.value(row) == parsed) return true;
    if (parsed.isEmpty()) m_signalEnums.remove(row); else m_signalEnums.insert(row, parsed);
    markSessionModified();
    for (int i = 0; i < m_plotRows*m_plotColumns; ++i) refreshStateTimeline(i);
    notifyPlotBindingsChanged(); return true;
}
bool AppController::addStateSignal(int plot, int row)
{
    if (sessionInteractionBlocked() || m_loading || m_exporting || plot < 0 || plot >= m_plotRows*m_plotColumns || row < 0 || row >= signalCount()) return false;
    const auto lanes = m_plotViews.value(plot).states.lanes;
    for (int i=0; i<lanes.size(); ++i) if (lanes[i].source == row)
        return configureStateLane(plot, i, {{"source", row}, {"name", signalName(row)}, {"visible", true}});
    return configureStateLane(plot, -1, {{"source", row}, {"name", signalName(row)}, {"visible", true}});
}
QVariantMap AppController::stateTimelineState(int index) const
{
    auto result = stateTimelineToJson(m_plotViews.value(index).states).toVariantMap();
    result["enabled"] = result["enabled"].toBool() && !m_trajectories.value(index).enabled;
    return result;
}
bool AppController::configureStateLane(int index, int lane, const QVariantMap &config)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows*m_plotColumns) return false;
    auto state = m_plotViews.value(index).states;
    if (lane < -1 || lane >= state.lanes.size()) return false;
    auto json = stateTimelineToJson(state); auto rows = json["lanes"].toArray();
    if (lane < 0) rows.append(QJsonObject::fromVariantMap(config)); else rows[lane] = QJsonObject::fromVariantMap(config);
    json["lanes"] = rows; json["enabled"] = true;
    StateTimelineConfig validated;
    if (!stateTimelineFromJson(json, signalCount(), &validated)) {
        setStatus(QStringLiteral("状态配置无效：检查来源、名称和显隐，最多 64 行")); return false;
    }
    m_plotViews[index].states = validated; m_trajectories[index].enabled = false;
    setActivePlot(index); refreshPlot(index, false);
    if (state.lanes.isEmpty()) fitPlots(true, false);
    notifyPlotBindingsChanged(); return true;
}
bool AppController::removeStateLane(int index, int lane)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows*m_plotColumns) return false;
    auto state = m_plotViews.value(index).states;
    if (lane < 0 || lane >= state.lanes.size()) return false;
    state.lanes.removeAt(lane); m_plotViews[index].states = state;
    setActivePlot(index); refreshPlot(index, false);
    if (state.lanes.isEmpty()) fitPlots(true, false);
    notifyPlotBindingsChanged(); return true;
}
bool AppController::moveStateLane(int index, int lane, int direction)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows*m_plotColumns) return false;
    auto &rows = m_plotViews[index].states.lanes; const int to = lane + (direction > 0 ? 1 : -1);
    if (lane < 0 || lane >= rows.size() || to < 0 || to >= rows.size()) return false;
    rows.move(lane, to); refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
void AppController::setStateTimelineEnabled(int index, bool enabled)
{
    if (sessionInteractionBlocked() || index < 0 || index >= m_plotRows*m_plotColumns) return;
    m_plotViews[index].states.enabled = enabled;
    if (enabled) m_trajectories[index].enabled = false;
    refreshPlot(index, false); notifyPlotBindingsChanged();
}
void AppController::attachStateTimeline(QObject *object, int index)
{
    if (!qobject_cast<StateTimelineItem *>(object) || index < 0 || index >= m_plotRows*m_plotColumns) return;
    m_stateTimelineItems[index] = object; refreshStateTimeline(index);
}
void AppController::refreshStateTimeline(int index)
{
    auto *item = qobject_cast<StateTimelineItem *>(m_stateTimelineItems.value(index)); if (!item) return;
    auto config = m_plotViews.value(index).states;
    if (!config.enabled || m_trajectories.value(index).enabled) config.lanes.clear();
    for (auto &lane : config.lanes) lane.values = m_signalEnums.value(lane.source);
    QVector<int> ids; for (const auto &lane : config.lanes) if (lane.visible && !ids.contains(lane.source)) ids.append(lane.source);
    item->setSources(config, m_seriesStore->snapshot(ids));
}
