// Per-subplot trajectory bindings, lifecycle and time-cursor synchronization.
#include "appcontroller.h"
#include "trajectoryitem.h"
#include "render/plotaxisutils.h"
#include <QScopedValueRollback>
#include <algorithm>

QVariantMap AppController::trajectoryState(int index) const
{
    const auto state = m_trajectories.value(index);
    return {{"enabled", state.enabled}, {"geographic", state.geographic},
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
    for (int id : axes) if (id < -1 || id >= signalCount()) return false;
    auto state = m_trajectories.value(index);
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
        for (int &id : state.axes) if (id == row) id = -1;
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
    std::array<PlotSeriesDataPtr, 3> axes;
    if (state.enabled) for (int a = 0; a < 3; ++a) {
        const auto snapshot = m_seriesStore->snapshot({state.axes[a]});
        if (!snapshot.series.isEmpty()) axes[a] = snapshot.series.first();
    }
    m_trajectoryPlots[index]->setAxes(axes, state.geographic);
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
    for (auto &state : m_trajectories) for (int &id : state.axes) {
        if (id < 0) continue;
        if (removed.contains(id)) id = -1;
        else id -= std::count_if(removed.cbegin(), removed.cend(), [id](int row) { return row < id; });
    }
    for (auto &state : m_trajectories) {
        QVector<int> ids;
        for (int id : state.signalIds) if (!removed.contains(id))
            ids.append(id - std::count_if(removed.cbegin(), removed.cend(), [id](int row) { return row < id; }));
        state.signalIds = std::move(ids);
    }
    for (int i = 0; i < m_trajectoryPlots.size(); ++i) refreshTrajectory(i);
}
