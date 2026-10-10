// GUI-thread orchestration of dependency-based immutable rule evaluation.
#include "appcontroller.h"
#include "plotitem.h"
#include <QScopedValueRollback>
#include <QThreadPool>
#include <QTimer>

struct AppController::ObjectJob {
    std::atomic_bool cancelled{false}, done{false};
    ObjectEvaluation result;
};
void AppController::cancelObjectEvaluation()
{
    if (m_objectJob) m_objectJob->cancelled = true;
    m_objectJob.reset();
}
void AppController::refreshComputedPlots(const QVector<PlotSeriesDataPtr> &series)
{
    QSet<int> changed;
    for (const auto &data : series) changed.insert(data->id);
    for (int index = 0; index < m_plots.size(); ++index) {
        bool affected = false;
        for (int row : m_signals->plotRows(index)) affected |= changed.contains(row);
        const auto trajectory = m_trajectories.constFind(index);
        if (trajectory != m_trajectories.cend()) for (const auto &track : trajectory->entries()) {
            for (int row : track.axes) affected |= changed.contains(row);
            for (int row : track.attitude.sources) affected |= changed.contains(row);
        }
        if (affected) refreshPlot(index, false);
    }
}
void AppController::scheduleObjectEvaluation()
{
    if (m_batchObjectChanges) return;
    // Publishing calculated samples is not a user edit. Callers mark definition changes.
    QScopedValueRollback<bool> publishing(m_applyingSession, true);
    cancelObjectEvaluation();
    syncObjectTrajectories();
    QVector<int> ids;
    QSet<QString> liveRules;
    for (const auto &object : m_objects) {
        for (const auto &field : object.fields) if (field.series >= 0) ids.append(field.series);
        for (const auto &rule : object.rules) {
            liveRules.insert(rule.id);
            for (const auto &output : rule.outputs) ids.append(output.series);
        }
    }
    const auto dirty = invalidatedObjectRules(m_objects, m_seriesStore->snapshot(ids), m_objectEvaluationStates);
    qsizetype retainedValues = 0;
    for (const auto &object : m_objects) for (const auto &rule : object.rules) {
        if (dirty.contains(rule.id)) {
            m_objectEvaluationStates.remove(rule.id);
            m_objectErrors.remove(rule.id);
        } else for (const auto &output : rule.outputs) {
            const auto data = m_seriesStore->snapshot({output.series});
            if (!data.series.isEmpty()) retainedValues += data.series.first()->values.size() * qsizetype(sizeof(double));
        }
    }
    for (auto it = m_objectEvaluationStates.begin(); it != m_objectEvaluationStates.end(); )
        if (!liveRules.contains(it.key())) it = m_objectEvaluationStates.erase(it); else ++it;
    for (auto it = m_objectErrors.begin(); it != m_objectErrors.end(); )
        if (!liveRules.contains(it.key())) it = m_objectErrors.erase(it); else ++it;
    // Invalidate results before starting a replacement, never display stale values.
    QVector<PlotSeriesDataPtr> empty;
    for (const auto &object : m_objects) for (const auto &rule : object.rules) if (dirty.contains(rule.id)) for (const auto &output : rule.outputs) {
        const auto current = m_seriesStore->snapshot({output.series});
        if (current.series.isEmpty()) continue;
        auto data = std::make_shared<PlotSeriesData>(*current.series.first());
        data->time.clear(); data->values.clear(); data->points.clear(); data->rangeIndex.reset(); empty.append(data);
    }
    m_seriesStore->publishComputed(empty);
    refreshComputedPlots(empty);
    if (dirty.isEmpty()) { emit objectsChanged(); return; }
    const auto job = std::make_shared<ObjectJob>(); m_objectJob = job;
    const auto objects = m_objects;
    const auto snapshot = m_seriesStore->snapshot(ids);
    QThreadPool::globalInstance()->start([job, objects, snapshot, retainedValues, dirty] {
        job->result = evaluateObjects(objects, snapshot, job->cancelled, retainedValues, &dirty);
        job->done.store(true, std::memory_order_release);
    });
    emit objectsChanged(); QTimer::singleShot(20, this, [this, job] { if (m_objectJob == job) pollObjectEvaluation(); });
}
void AppController::pollObjectEvaluation()
{
    if (!m_objectJob) return;
    if (!m_objectJob->done.load(std::memory_order_acquire)) {
        const auto job = m_objectJob;
        QTimer::singleShot(20, this, [this, job] { if (m_objectJob == job) pollObjectEvaluation(); }); return;
    }
    const auto job = std::move(m_objectJob);
    const auto &result = job->result;
    QScopedValueRollback<bool> publishing(m_applyingSession, true);
    m_seriesStore->publishComputed(result.series);
    for (auto it = result.states.cbegin(); it != result.states.cend(); ++it) m_objectEvaluationStates.insert(it.key(), it.value());
    for (auto it = result.errors.cbegin(); it != result.errors.cend(); ++it) m_objectErrors.insert(it.key(), it.value());
    refreshComputedPlots(result.series);
    notifyPlotBindingsChanged(); emit objectsChanged();
}
