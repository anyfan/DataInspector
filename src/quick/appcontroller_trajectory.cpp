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
    const auto &attitude = state.attitude;
    return {{"tracks", tracks}, {"active", state.active}, {"name", state.name}, {"color", state.color}, {"width", state.width}, {"visible", state.visible},
            {"attitudeMode", attitude.mode}, {"attitudeSources", QVariantList{attitude.sources[0], attitude.sources[1], attitude.sources[2], attitude.sources[3]}},
            {"radians", attitude.radians}, {"order", attitude.order}, {"scalarLast", attitude.scalarLast}, {"navigationToBody", attitude.navigationToBody},
            {"enabled", state.enabled}, {"geographic", state.geographic},
            {"signalCount", state.signalIds.size()},
            {"planar", std::count_if(state.axes.cbegin(), state.axes.cend(), [](int id) { return id >= 0; }) == 2},
            {"x", state.axes[0]}, {"y", state.axes[1]}, {"z", state.axes[2]}};
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
    state.enabled = enabled; state.axes = axes; state.geographic = geographic;
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
        && std::all_of(state.axes.cbegin(), state.axes.cend(), [](int id) { return id < 0; }))
        state.geographic = true;
    if (state.signalIds.isEmpty()) {
        state.signalIds = m_signals->plotRows(index);
        for (int row : state.signalIds) {
            if (std::find(state.axes.cbegin(), state.axes.cend(), row) != state.axes.cend()) continue;
            const auto empty = std::find(state.axes.begin(), state.axes.end(), -1);
            if (empty == state.axes.end()) break;
            *empty = row;
        }
    }
    if (configureTrajectory(index, true, state.axes[0], state.axes[1], state.axes[2], state.geographic))
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
        const auto empty = std::find(state.axes.begin(), state.axes.end(), -1);
        if (empty != state.axes.end()) *empty = row;
    } else {
        state.signalIds.removeAll(row);
        auto tracks = state.entries();
        for (auto &track : tracks) {
            for (int &id : track.axes) if (id == row) id = -1;
            for (int &id : track.attitude.sources) if (id == row) id = -1;
        }
        state.tracks = tracks; static_cast<SessionTrajectoryEntry &>(state) = tracks[state.active];
    }
    m_trajectories.insert(index, state);
    return configureTrajectory(index, true, state.axes[0], state.axes[1], state.axes[2], state.geographic);
}
bool AppController::bindTrajectoryAxis(int index, int axis, int row)
{
    if (sessionInteractionBlocked() || m_loading || axis < 0 || axis > 2) return false;
    auto state = m_trajectories.value(index);
    if (!state.enabled || (row != -1 && !state.signalIds.contains(row))) return false;
    if (row >= 0) {
        const auto existing = std::find(state.axes.begin(), state.axes.end(), row);
        if (existing != state.axes.end()) *existing = state.axes[axis];
    }
    state.axes[axis] = row;
    setActivePlot(index);
    return configureTrajectory(index, true, state.axes[0], state.axes[1], state.axes[2], state.geographic);
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
void AppController::remapTrajectoryAxes(const QVector<int> &removed)
{
    for (auto &state : m_trajectories) {
        auto tracks = state.entries();
        const auto remap = [&](int &id) {
            if (id < 0) return;
            if (removed.contains(id)) id = -1;
            else id -= std::count_if(removed.cbegin(), removed.cend(), [id](int row) { return row < id; });
        };
        for (auto &track : tracks) {
            for (int &id : track.axes) remap(id);
            for (int &id : track.attitude.sources) remap(id);
        }
        state.tracks = tracks; static_cast<SessionTrajectoryEntry &>(state) = tracks[state.active];
    }
    for (auto &state : m_trajectories) {
        QVector<int> ids;
        for (int id : state.signalIds) if (!removed.contains(id))
            ids.append(id - std::count_if(removed.cbegin(), removed.cend(), [id](int row) { return row < id; }));
        state.signalIds = std::move(ids);
    }
    for (int i = 0; i < m_trajectoryPlots.size(); ++i) refreshTrajectory(i);
}

bool AppController::addTrajectory(int index)
{
    if (sessionInteractionBlocked() || m_loading || index < 0 || index >= m_plotRows * m_plotColumns) return false;
    auto &state = m_trajectories[index];
    if (state.tracks.size() >= 64) return false;
    state.tracks = state.entries();
    SessionTrajectoryEntry entry; entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = QStringLiteral("航迹 %1").arg(state.tracks.size() + 1); entry.geographic = state.geographic;
    const QStringList colors{"#0072bd", "#d95319", "#7e2f8e", "#77ac30", "#4dbeee", "#edb120"};
    entry.color = QColor(colors[state.tracks.size() % colors.size()]);
    state.tracks.append(entry); state.active = state.tracks.size() - 1;
    static_cast<SessionTrajectoryEntry &>(state) = entry; state.enabled = true;
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
    state.tracks = tracks; static_cast<SessionTrajectoryEntry &>(state) = tracks[state.active];
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
    auto &state = m_trajectories[index]; state.name = name; state.color = color; state.width = width; state.visible = visible;
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
bool AppController::configureAttitude(int index, int mode, int a, int b, int c, int d,
    bool radians, int order, bool scalarLast, bool navigationToBody)
{
    if (sessionInteractionBlocked() || m_loading || !m_trajectories.contains(index) || mode < 0 || mode > 2 || order < 0 || order > 1) return false;
    auto &state = m_trajectories[index];
    const std::array<int, 4> sources{{a, b, c, d}};
    for (int id : sources) if (id < -1 || (id >= 0 && !state.signalIds.contains(id))) return false;
    state.attitude = {mode, sources, radians, scalarLast, navigationToBody, order};
    refreshPlot(index, false); notifyPlotBindingsChanged(); return true;
}
