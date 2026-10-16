#include "automationserver.h"
#include "commandcatalog.h"
#include "controller/appcontroller.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUuid>

namespace {
QJsonObject fail(const QString &code, const QString &message) { return {{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", message}}}}; }
QJsonObject ok(const QJsonValue &result) { return {{"ok", true}, {"result", result}}; }
bool terminal(const QJsonObject &data) { return data.value("state") != "running" && data.value("state") != "cancelling"; }
}

QString AutomationServer::generation() const { return m_epoch + ':' + QString::number(m_controller->automationSnapshot().generation); }
QJsonObject AutomationServer::jobValue(const QString &id) const
{
    const auto it = m_jobs.constFind(id); if (it == m_jobs.cend()) return {};
    auto value = it->data; value["result"] = it->result; return value;
}

void AutomationServer::completeJob(const QString &id, const QJsonObject &result, const QString &error, bool cancelled)
{
    auto it = m_jobs.find(id); if (it == m_jobs.end() || terminal(it->data)) return;
    cancelled |= it->cancelled->load();
    it->result = result;
    it->data["state"] = cancelled ? "cancelled" : error.isEmpty() ? "succeeded" : "failed";
    it->data["progress"] = 100; it->data["finishedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!error.isEmpty()) it->data["error"] = QJsonObject{{"code", cancelled ? "cancelled" : "operation_failed"}, {"message", error}};
    if (m_activeJob == id) m_activeJob.clear();
}

void AutomationServer::dispatch(QTcpSocket *socket, const QJsonObject &request)
{
    if (request.value("token").toString().isEmpty() || request.value("token").toString() != m_token) { send(socket, fail("unauthorized", "Invalid connection token")); return; }
    for (auto it = request.begin(); it != request.end(); ++it) if (!QStringList{"token", "command", "params", "requestId"}.contains(it.key())) { send(socket, fail("invalid_params", "Unknown request field")); return; }
    const auto name = request.value("command").toString();
    const auto spec = commandSpec(name);
    if (spec.isEmpty()) { send(socket, fail("unknown_command", "Use help to discover commands")); return; }
    const auto paramsValue = request.contains("params") ? request.value("params") : QJsonValue(QJsonObject{});
    const auto error = validateCommandValue(paramsValue, spec.value("inputSchema").toObject());
    if (!error.isEmpty()) { send(socket, fail("invalid_params", error)); return; }
    const auto params = paramsValue.toObject();
    const auto requestId = request.value("requestId").toString();
    if (request.contains("requestId") && (!request.value("requestId").isString() || requestId.isEmpty() || requestId.size() > 128)) { send(socket, fail("invalid_params", "requestId must be a nonempty string up to 128 characters")); return; }
    if (!requestId.isEmpty() && !spec.value("mutating").toBool() && !spec.value("asynchronous").toBool()) { send(socket, fail("invalid_params", "requestId is supported for mutations and asynchronous operations")); return; }
    const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject{{"command", name}, {"params", params}}).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    if (!requestId.isEmpty() && m_requests.contains(requestId)) {
        const auto jobId = m_requests.value(requestId); const auto &job = m_jobs[jobId];
        if (job.fingerprint != fingerprint) { send(socket, fail("idempotency_conflict", "requestId was already used with different command or parameters")); return; }
        auto response = job.initial.isEmpty() ? ok(QJsonObject{{"accepted", true}}) : job.initial;
        response["task"] = jobValue(jobId); response["replayed"] = true; send(socket, response); return;
    }
    if (name == "jobs") {
        QJsonArray list; const int offset = params.value("offset").toInt(), limit = params.value("limit").toInt(20);
        for (int i = offset; i < qMin(m_jobOrder.size(), qsizetype(offset + limit)); ++i) list.append(jobValue(m_jobOrder[i]));
        send(socket, ok(QJsonObject{{"jobs", list}, {"total", m_jobOrder.size()}})); return;
    }
    if (name == "job_status" || name == "job_cancel") {
        const auto jobId = params.value("id").toString();
        if (!m_jobs.contains(jobId)) { send(socket, fail("unknown_job", "Task is not retained by this server instance")); return; }
        auto &job = m_jobs[jobId];
        if (name == "job_cancel" && !terminal(job.data)) {
            if (!job.data.value("cancelable").toBool()) { send(socket, fail("not_cancelable", "This operation has already committed its synchronous change")); return; }
            job.cancelled->store(true); job.data["state"] = "cancelling";
            if (job.watch == "load" || job.watch == "restore_session") m_controller->cancelAutomation(job.watch);
            else if (job.watch == "derive" || job.watch == "restore_derive") { m_controller->cancelAutomation("derive"); completeJob(jobId, {}, "Evaluation cancelled", true); }
            else if (job.watch == "export") m_controller->cancelAutomation("export");
        }
        send(socket, ok(jobValue(jobId))); return;
    }
    const bool asynchronous = spec.value("asynchronous").toBool(), mutating = spec.value("mutating").toBool();
    if ((asynchronous && !m_activeJob.isEmpty()) || ((asynchronous || mutating) && (m_controller->loading() || m_controller->restoringSession() || m_controller->exporting() || m_controller->deriving()))) {
        send(socket, fail("busy", "Another asynchronous operation is running; inspect its task")); return;
    }
    if (mutating || asynchronous || !requestId.isEmpty()) {
        if (m_jobs.size() >= 512) {
            for (const auto &candidate : std::as_const(m_jobOrder)) {
                const auto &old = m_jobs[candidate];
                if (old.requestId.isEmpty() && terminal(old.data)) { m_jobs.remove(candidate); m_jobOrder.removeOne(candidate); break; }
            }
        }
        if (m_jobs.size() >= 512) { send(socket, fail("task_capacity", "512 retained tasks have active work or idempotency keys; save workspace and restart before adding more")); return; }
        const auto jobId = QUuid::createUuid().toString(QUuid::Id128); Job job;
        if (name == "load" || name == "restore_session") job.watch = name;
        else if (name == "capture_window" || name == "export_logical") job.watch = "export";
        else if (name == "merge_signals" || name == "merge_execute" || name == "rules_retry" || name == "rule_update") job.watch = "derive";
        else if (name == "object_trajectory" || name == "trajectory_view") job.watch = "render";
        else if (name == "aircraft_setup" || name == "mapping_apply") job.watch = "setup";
        else if (name == "merge_preview") job.watch = "preview";
        else if (asynchronous) job.watch = "analysis";
        job.requestId = requestId; job.fingerprint = fingerprint;
        job.data = {{"id", jobId}, {"command", name}, {"state", "running"}, {"progress", 0}, {"generation", generation()},
            {"startedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"cancelable", QStringList{"load", "restore_session", "export", "derive", "preview", "analysis"}.contains(job.watch)}};
        m_jobs.insert(jobId, job); m_jobOrder.append(jobId);
        if (!requestId.isEmpty()) m_requests.insert(requestId, jobId);
        if (asynchronous) m_activeJob = jobId;
        socket->setProperty("jobId", jobId);
    }
    dispatchOperation(socket, {{"token", m_token}, {"command", name}, {"params", params}});
}

void AutomationServer::pollJobs()
{
    if (m_activeJob.isEmpty() || !m_jobs.contains(m_activeJob)) return;
    const auto id = m_activeJob; auto &job = m_jobs[id];
    if ((job.watch == "render" || job.watch == "setup") && QDateTime::fromString(job.data.value("startedAt").toString(), Qt::ISODateWithMs).msecsTo(QDateTime::currentDateTimeUtc()) > 30000) {
        completeJob(id, job.result, QStringLiteral("View did not become ready within 30 seconds")); return;
    }
    if (job.watch == "load" || job.watch == "restore_session") job.data["progress"] = m_controller->loadingProgress();
    else if (job.watch == "export") job.data["progress"] = m_controller->exportProgress();
    else if (job.watch == "derive" || job.watch == "restore_derive") {
        if (m_controller->deriving()) {
            const auto p = m_controller->objectEvaluationProgress(); job.data["progress"] = p.value("total").toInt() > 0 ? p.value("completed").toInt() * 99 / p.value("total").toInt() : 0; return;
        }
        QSet<QString> tracked;
        for (const auto &v : job.result.value("outputs").toArray()) tracked.insert(v.toObject().value("rule").toString());
        QJsonArray errors, warnings; int checked = 0;
        for (const auto &o : m_controller->dataObjects()) for (const auto &r : o.toMap().value("rules").toList()) {
            const auto rule = r.toMap(); if (!tracked.isEmpty() && !tracked.contains(rule.value("id").toString())) continue;
            ++checked; if (!rule.value("error").toString().isEmpty()) {
                bool hasValues = false;
                for (const auto &output : rule.value("outputs").toList()) { const auto snap = m_controller->automationSnapshot({output.toMap().value("series").toInt()}); hasValues |= !snap.series.isEmpty() && snap.series.first()->sampleCount() > 0; }
                (hasValues ? warnings : errors).append(QJsonObject::fromVariantMap(rule));
            }
        }
        auto result = job.result; result["errors"] = errors; result["warnings"] = warnings; result["checkedRules"] = checked;
        completeJob(id, result, errors.isEmpty() ? QString() : QStringLiteral("One or more rules failed; inspect task.result.errors"));
    } else if (job.watch == "render") {
        const int plot = job.data.value("plot").toInt();
        auto state = QJsonObject::fromVariantMap(m_controller->trajectoryRenderState(plot));
        if (state.value("ready").toBool()) completeJob(id, state);
        else if (state.value("attached").toBool() && !state.value("pending").toBool()) {
            const int attempts = job.data.value("attempts").toInt() + 1; job.data["attempts"] = attempts;
            if (attempts > 80) completeJob(id, state, state.value("error").toString());
        }
    } else if (job.watch == "setup") {
        if (m_controller->deriving()) return;
        const auto object = job.result.value("object").toString(); auto check = QJsonObject::fromVariantMap(m_controller->aircraftCheck(object));
        if (job.data.contains("plot")) {
            const auto state = m_controller->trajectoryRenderState(job.data.value("plot").toInt());
            if (!state.value("attached").toBool() || state.value("pending").toBool()) return;
            if (!state.value("ready").toBool()) { completeJob(id, check, state.value("error").toString()); return; }
        }
        completeJob(id, check, check.value("bindingsReady").toBool() ? QString() : QStringLiteral("Aircraft has missing position or attitude bindings"));
    }
}
