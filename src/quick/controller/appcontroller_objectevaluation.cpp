// GUI-thread orchestration of dependency-based immutable rule evaluation.
#include "appcontroller.h"
#include "items/plotitem.h"
#include <QScopedValueRollback>
#include <QThreadPool>
#include <QTimer>

struct AppController::ObjectJob {
    std::atomic_bool cancelled{false}, done{false};
    ObjectEvaluation result;
    QString discoveryObject;
    quint64 discoveryGeneration = 0;
    QVector<PacketLossGroup> discoveryGroups;
    std::atomic<int> completed{0};
    int total = 0;
};
bool AppController::identifyPacketLoss(const QString &objectId)
{
    if (m_loading || m_exporting || deriving() || sessionInteractionBlocked()) return false;
    bool found = false;
    for (const auto &object : m_objects) if (object.id == objectId && object.type == "packetloss") found = true;
    if (!found) { setStatus(QStringLiteral("请先选择丢包检测对象")); return false; }
    QSet<int> computedRows;
    for (const auto &object : m_objects) for (const auto &rule : object.rules)
        for (const auto &output : rule.outputs) computedRows.insert(output.series);
    QVector<int> ids;
    for (int row = 0; row < signalCount(); ++row) if (!computedRows.contains(row)) ids.append(row);
    if (ids.isEmpty()) { setStatus(QStringLiteral("请先加载需要检测的原始数据")); return false; }
    const auto snapshot = m_seriesStore->snapshot(ids);
    const auto job = std::make_shared<ObjectJob>(); job->discoveryObject = objectId;
    job->discoveryGeneration = m_seriesStore->generation(); job->total = ids.size(); m_objectJob = job;
    QThreadPool::globalInstance()->start([job, snapshot] {
        job->discoveryGroups = identifyPacketLossGroups(snapshot, job->cancelled, &job->completed);
        job->done.store(true, std::memory_order_release);
    });
    setStatus(QStringLiteral("正在自动识别采样时间组…"));
    emit objectsChanged(); QTimer::singleShot(20, this, [this, job] { if (m_objectJob == job) pollObjectEvaluation(); });
    return true;
}

void AppController::cancelObjectEvaluation()
{
    if (m_objectJob) { m_objectJob->cancelled = true; emit objectEvaluationAborted(); }
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
    const auto job = std::make_shared<ObjectJob>(); job->total = dirty.size(); m_objectJob = job;
    const auto objects = m_objects;
    const auto snapshot = m_seriesStore->snapshot(ids);
    QThreadPool::globalInstance()->start([job, objects, snapshot, retainedValues, dirty] {
        job->result = evaluateObjects(objects, snapshot, job->cancelled, retainedValues, &dirty, true, &job->completed);
        job->done.store(true, std::memory_order_release);
    });
    emit objectsChanged(); QTimer::singleShot(20, this, [this, job] { if (m_objectJob == job) pollObjectEvaluation(); });
}

QVariantMap AppController::objectEvaluationProgress() const
{
    return {{"completed", m_objectJob ? m_objectJob->completed.load() : 0}, {"total", m_objectJob ? m_objectJob->total : 0}};
}
void AppController::pollObjectEvaluation()
{
    if (!m_objectJob) return;
    if (!m_objectJob->done.load(std::memory_order_acquire)) {
        const auto job = m_objectJob;
        QTimer::singleShot(20, this, [this, job] { if (m_objectJob == job) pollObjectEvaluation(); }); return;
    }
    const auto job = std::move(m_objectJob);
    if (!job->discoveryObject.isEmpty()) {
        if (job->cancelled.load() || job->discoveryGeneration != m_seriesStore->generation()) {
            setStatus(QStringLiteral("识别期间数据已变更，请重新自动识别")); emit objectsChanged(); return;
        }
        if (!applyPacketLossGroups(job->discoveryObject, job->discoveryGroups)) emit objectsChanged();
        return;
    }
    const auto &result = job->result;
    QScopedValueRollback<bool> publishing(m_applyingSession, true);
    m_seriesStore->publishComputed(result.series);
    for (auto &object : m_objects) for (auto &rule : object.rules) {
        auto state = result.states.value(rule.id);
        if (!result.states.contains(rule.id)) continue;
        // Materialize the initial estimate once; later recalculation preserves the editable value.
        if (rule.operation == "packetloss" && rule.samplePeriod == 0 && state.samplePeriod > 0 && !result.errors.contains(rule.id)) {
            rule.samplePeriod = state.samplePeriod;
            state.definition.insert("samplePeriod", rule.samplePeriod);
        }
        m_objectEvaluationStates.insert(rule.id, state);
    }
    for (auto it = result.errors.cbegin(); it != result.errors.cend(); ++it) m_objectErrors.insert(it.key(), it.value());
    refreshComputedPlots(result.series);
    notifyPlotBindingsChanged(); emit objectsChanged();
}
