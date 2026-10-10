// Per-subplot trajectory bindings, lifecycle and time-cursor synchronization.
#include "appcontroller.h"
#include "trajectoryitem.h"
#include "render/plotaxisutils.h"
#include <QScopedValueRollback>
#include <algorithm>
#include <QUuid>

QVariantMap AppController::trajectoryState(int index) const
{
    const auto state = m_trajectories.value(index);
    QVariantList tracks;
    for (const auto &entry : state.entries()) tracks.append(QVariantMap{{"id", entry.id}, {"name", entry.name}, {"visible", entry.visible}, {"color", entry.color}});
    const auto &attitude = state.activeEntry().attitude;
    return {{"tracks", tracks}, {"active", state.active}, {"objectId", state.activeEntry().objectId}, {"name", state.activeEntry().name}, {"color", state.activeEntry().color}, {"width", state.activeEntry().width}, {"visible", state.activeEntry().visible},
            {"attitudeMode", attitude.mode}, {"attitudeSources", QVariantList{attitude.sources[0], attitude.sources[1], attitude.sources[2], attitude.sources[3]}},
            {"radians", attitude.radians}, {"order", attitude.order}, {"scalarLast", attitude.scalarLast}, {"navigationToBody", attitude.navigationToBody},
            {"enabled", state.enabled}, {"geographic", state.activeEntry().geographic},
            {"signalCount", state.signalIds.size()},
            {"planar", std::count_if(state.activeEntry().axes.cbegin(), state.activeEntry().axes.cend(), [](int id) { return id >= 0; }) == 2},
            {"x", state.activeEntry().axes[0]}, {"y", state.activeEntry().axes[1]}, {"z", state.activeEntry().axes[2]}};
}
QVariantList AppController::trajectorySignalOptions(int index) const
{
    QVariantList result{QVariantMap{{"id", -1}, {"label", QStringLiteral("不选（留空）")}}};
    for (int row : m_trajectories.value(index).signalIds)
        result.append(QVariantMap{{"id", row}, {"name", m_signals->nameAt(row)}, {"label", m_signals->groupAt(row)
            + QLatin1Char('/') + m_signals->nameAt(row)}});
    return result;
}
bool AppController::configureTrajectory(int index, bool enabled, int x, int y, int z, bool geographic)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows * m_plotColumns) return false;
    const std::array<int, 3> axes{{x, y, z}};
    QSet<int> used;
    for (int id : axes) {
        if (id < -1 || id >= signalCount() || (id >= 0 && used.contains(id))) return false;
        if (id >= 0) used.insert(id);
    }
    auto state = m_trajectories.value(index);
    for (int i = 0; i < state.tracks.size(); ++i) if (i != state.active) {
        const auto &other = state.tracks[i];
        if (other.geographic != geographic && std::any_of(other.axes.cbegin(), other.axes.cend(), [](int id) { return id >= 0; })) return false;
    }
    if (state.activeEntry().axes != axes || state.activeEntry().geographic != geographic) state.activeEntry().objectId.clear();
    state.enabled = enabled; state.activeEntry().axes = axes; state.activeEntry().geographic = geographic;
    for (int id : axes) if (id >= 0 && !state.signalIds.contains(id)) state.signalIds.append(id);
    m_trajectories.insert(index, state);

    if (enabled && !m_initialSignalFitDone) {
        const auto bounds = PlotSeriesStore::timeBounds(m_seriesStore->snapshot({x, y, z}));
        if (bounds) {
            const auto range = paddedPlotRange(bounds->first, bounds->second, .02, .5);
            if (range) { applySharedXRange(range->first, range->second); m_initialSignalFitDone = true; }
        }
    }
    refreshPlot(index, false); notifyPlotBindingsChanged();
    return true;
}
void AppController::enterTrajectoryMode(int index)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows * m_plotColumns) return;
    auto &state = m_trajectories[index];
    if (!state.enabled && state.signalIds.isEmpty()
        && std::all_of(state.activeEntry().axes.cbegin(), state.activeEntry().axes.cend(), [](int id) { return id < 0; }))
        state.activeEntry().geographic = true;
    if (state.signalIds.isEmpty()) {
        state.signalIds = m_signals->plotRows(index);
        for (int row : state.signalIds) {
            if (std::find(state.activeEntry().axes.cbegin(), state.activeEntry().axes.cend(), row) != state.activeEntry().axes.cend()) continue;
            const auto empty = std::find(state.activeEntry().axes.begin(), state.activeEntry().axes.end(), -1);
            if (empty == state.activeEntry().axes.end()) break;
            *empty = row;
        }
    }
    if (configureTrajectory(index, true, state.activeEntry().axes[0], state.activeEntry().axes[1], state.activeEntry().axes[2], state.activeEntry().geographic))
        setActivePlot(index);
}
bool AppController::setTrajectorySignal(int index, int row, bool selected)
{
    if (row < 0 || row >= signalCount() || m_loading || sessionInteractionBlocked()) return false;
    auto state = m_trajectories.value(index);
    if (!state.enabled) return false;
    if (selected) {
        if (state.signalIds.contains(row)) return true;
        state.signalIds.append(row);
        const auto empty = std::find(state.activeEntry().axes.begin(), state.activeEntry().axes.end(), -1);
        if (empty != state.activeEntry().axes.end()) *empty = row;
    } else {
        state.signalIds.removeAll(row);
        auto tracks = state.entries();
        for (auto &track : tracks) {
            for (int &id : track.axes) if (id == row) id = -1;
            for (int &id : track.attitude.sources) if (id == row) id = -1;
        }
        state.tracks = tracks;
    }
    m_trajectories.insert(index, state);
    return configureTrajectory(index, true, state.activeEntry().axes[0], state.activeEntry().axes[1], state.activeEntry().axes[2], state.activeEntry().geographic);
}
bool AppController::bindTrajectoryAxis(int index, int axis, int row)
{
    if (sessionInteractionBlocked() || m_loading || axis < 0 || axis > 2) return false;
    auto state = m_trajectories.value(index);
    if (!state.enabled || (row != -1 && !state.signalIds.contains(row))) return false;
    if (row >= 0) {
        const auto existing = std::find(state.activeEntry().axes.begin(), state.activeEntry().axes.end(), row);
        if (existing != state.activeEntry().axes.end()) *existing = state.activeEntry().axes[axis];
    }
    state.activeEntry().axes[axis] = row;
    setActivePlot(index);
    return configureTrajectory(index, true, state.activeEntry().axes[0], state.activeEntry().axes[1], state.activeEntry().axes[2], state.activeEntry().geographic);
}
void AppController::syncSignalSelection(bool refresh)
{
    const auto state = m_trajectories.value(m_signals->activePlot());
    if (!state.enabled) { m_signals->setSelectionOverride(std::nullopt, refresh); return; }
    QSet<int> rows;
    for (int id : state.signalIds) if (id >= 0 && id < signalCount()) rows.insert(id);
    m_signals->setSelectionOverride(std::move(rows), refresh);
}
void AppController::attachTrajectory(QObject *object, int index)
{
    auto *item = qobject_cast<TrajectoryItem *>(object);
    if (!item || index < 0 || index >= m_plotRows * m_plotColumns) return;
    if (m_trajectoryPlots.size() <= index) m_trajectoryPlots.resize(index + 1);
    if (m_trajectoryPlots[index] == item) { refreshTrajectory(index); return; }
    if (m_trajectoryPlots[index]) disconnect(m_trajectoryPlots[index], nullptr, this, nullptr);
    m_trajectoryPlots[index] = item;
    item->setCamera(m_trajectories.value(index).camera);
    connect(item, &TrajectoryItem::activated, this, [this, index] { setActivePlot(index); });
    connect(item, &TrajectoryItem::cameraAboutToChange, this, &AppController::recordViewChange);
    connect(item, &TrajectoryItem::viewInteractionStarted, this, &AppController::beginViewChange);
    connect(item, &TrajectoryItem::viewInteractionFinished, this, &AppController::endViewChange);
    connect(item, &TrajectoryItem::cameraChanged, this, [this, index, item] {
        markSessionModified();
        m_trajectories[index].camera = item->camera();
    });
    refreshTrajectory(index);
}
void AppController::detachTrajectory(QObject *object, int index)
{
    if (index < 0 || index >= m_trajectoryPlots.size() || m_trajectoryPlots[index] != object) return;
    disconnect(object, nullptr, this, nullptr); m_trajectoryPlots[index].clear();
}
void AppController::refreshTrajectory(int index)
{
    if (index < 0 || index >= m_trajectoryPlots.size() || !m_trajectoryPlots[index]) return;
    const auto state = m_trajectories.value(index);
    QVector<TrajectorySource> sources;
    if (state.enabled) for (const auto &track : state.entries()) {
        TrajectorySource source; source.id = track.id; source.name = track.name;
        source.geographic = track.geographic; source.visible = track.visible;
        source.color = track.color; source.width = track.width; source.attitude = track.attitude;
        for (int a = 0; a < 3; ++a) {
            const auto snapshot = m_seriesStore->snapshot({track.axes[a]});
            if (!snapshot.series.isEmpty()) source.axes[a] = snapshot.series.first();
        }
        for (int a = 0; a < 4; ++a) {
            const auto snapshot = m_seriesStore->snapshot({track.attitude.sources[a]});
            if (!snapshot.series.isEmpty()) source.attitudeSources[a] = snapshot.series.first();
        }
        sources.append(source);
    }
    m_trajectoryPlots[index]->setSources(sources);
    syncTrajectoryCursors();
}
void AppController::syncTrajectoryCursors()
{
    for (const auto &item : m_trajectoryPlots) if (item)
        item->setTimeCursor(m_sessionCursor.mode, m_sessionCursor.x1, m_sessionCursor.x2,
                            m_sharedXMinimum, m_sharedXMaximum);
}
void AppController::remapSignalReferences(const QVector<int> &removed)
{
    // SignalModel and Store have already removed these rows. Build the mapping
    // once and update all references before publishing any refreshed view.
    const int previousCount = signalCount() + removed.size();
    QVector<int> mapping(previousCount, -1);
    const QSet<int> removedSet(removed.cbegin(), removed.cend());
    int next = 0;
    for (int row = 0; row < previousCount; ++row)
        if (!removedSet.contains(row)) mapping[row] = next++;
    const auto remap = [&](int &id) {
        id = id >= 0 && id < mapping.size() ? mapping[id] : -1;
    };
    for (auto &object : m_objects) {
        for (auto &field : object.fields) remap(field.series);
        for (auto &rule : object.rules)
            for (auto &output : rule.outputs) remap(output.series);
    }
    for (auto &state : m_trajectories) {
        for (auto &track : state.tracks) {
            for (int &id : track.axes) remap(id);
            for (int &id : track.attitude.sources) remap(id);
        }
        QVector<int> ids;
        for (int id : state.signalIds) {
            remap(id);
            if (id >= 0) ids.append(id);
        }
        state.signalIds = std::move(ids);
    }
}

bool AppController::addTrajectory(int index)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows * m_plotColumns) return false;
    auto &state = m_trajectories[index];
    if (state.tracks.size() >= 64) return false;
    SessionTrajectoryEntry entry; entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = QStringLiteral("航迹 %1").arg(state.tracks.size() + 1); entry.geographic = state.activeEntry().geographic;
    const QStringList colors{"#0072bd", "#d95319", "#7e2f8e", "#77ac30", "#4dbeee", "#edb120"};
    entry.color = QColor(colors[state.tracks.size() % colors.size()]);
    state.tracks.append(entry); state.active = state.tracks.size() - 1;
    state.enabled = true;
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
bool AppController::removeTrajectory(int index, int track)
{
    if (sessionInteractionBlocked() || m_loading || !m_trajectories.contains(index)) return false;
    auto &state = m_trajectories[index];
    if (track < 0 || track >= state.tracks.size()) return false;
    auto tracks = state.entries(); tracks.removeAt(track);
    if (tracks.isEmpty()) tracks.append(SessionTrajectoryEntry{});
    state.active = track < state.active ? state.active - 1 : qMin(state.active, int(tracks.size()) - 1);
    state.tracks = tracks;
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
bool AppController::selectTrajectory(int index, int track)
{
    if (sessionInteractionBlocked() || m_loading || !m_trajectories.contains(index)) return false;
    auto &state = m_trajectories[index];
    if (track < 0 || track >= state.tracks.size()) return false;
    state.select(track); setActivePlot(index); notifyPlotBindingsChanged(); return true;
}
bool AppController::styleTrajectory(int index, const QString &name, const QColor &color, double width, bool visible)
{
    if (sessionInteractionBlocked() || m_loading || !m_trajectories.contains(index) || name.trimmed().isEmpty()
        || name.size() > 256 || name.contains(QChar::Null) || !color.isValid() || !qIsFinite(width) || width < 1 || width > 12) return false;
    auto &state = m_trajectories[index]; state.activeEntry().name = name; state.activeEntry().color = color; state.activeEntry().width = width; state.activeEntry().visible = visible;
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
bool AppController::configureAttitude(int index, int mode, int a, int b, int c, int d,
    bool radians, int order, bool scalarLast, bool navigationToBody)
{
    if (sessionInteractionBlocked() || m_loading || !m_trajectories.contains(index) || mode < 0 || mode > 2 || order < 0 || order > 1) return false;
    auto &state = m_trajectories[index];
    const std::array<int, 4> sources{{a, b, c, d}};
    for (int id : sources) if (id < -1 || (id >= 0 && !state.signalIds.contains(id))) return false;
    const TrajectoryAttitude configured{mode, sources, radians, scalarLast, navigationToBody, order};
    if (!(state.activeEntry().attitude == configured)) state.activeEntry().objectId.clear();
    state.activeEntry().attitude = configured;
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
