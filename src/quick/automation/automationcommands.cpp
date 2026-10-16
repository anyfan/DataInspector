#include "automationserver.h"
#include "commandcatalog.h"
#include "controller/appcontroller.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QTcpSocket>
#include <QUuid>

namespace {
QJsonObject ok(const QJsonValue &v = QJsonObject{}) { return {{"ok", true}, {"result", v}}; }
QJsonObject fail(const QString &code, const QString &text) { return {{"ok", false}, {"error", QJsonObject{{"code", code}, {"message", text}}}}; }
}

bool AutomationServer::dispatchExtended(QTcpSocket *socket, const QString &command, const QJsonObject &p)
{
    auto *c = m_controller;
    const auto jobId = socket->property("jobId").toString();
    if (p.contains("plot") && m_jobs.contains(jobId)) m_jobs[jobId].data["plot"] = p.value("plot");
    if (command == "rules") {
        reply(socket, ok(QJsonObject::fromVariantMap(c->ruleResults(p.value("search").toString(), p.value("state").toString(), p.value("offset").toInt(), p.value("limit").toInt(100))))); return true;
    }
    if (command == "rules_retry") { c->retryObjectRules(); reply(socket, ok(QJsonObject{{"accepted", true}, {"completed", false}})); return true; }
    if (command == "rule_update") {
        if (p.value("generation").toString() != generation()) { reply(socket, fail("stale_generation", "Discover current definitions again")); return true; }
        if (!c->editObjectRule(p.value("object").toString(), p.value("rule").toString(), p.value("config").toObject().toVariantMap())) {
            reply(socket, fail("rejected", "Rule update rejected; verify object/input identities, parameters and unchanged output count")); return true;
        }
        reply(socket, ok(QJsonObject{{"accepted", true}, {"completed", false}, {"outputs", QJsonArray{QJsonObject{{"rule", p.value("rule")}}}}})); return true;
    }
    if (command == "aircraft_check") { const auto result = c->aircraftCheck(p.value("object").toString()); reply(socket, result.contains("error") ? fail("rejected", result.value("error").toString()) : ok(QJsonObject::fromVariantMap(result))); return true; }
    if (command == "logical_view") { c->setLogicalSignalView(p.value("enabled").toBool()); reply(socket, ok(QJsonObject{{"enabled", c->logicalSignalView()}, {"logicalCount", c->logicalSignalRows().size()}})); return true; }
    if (command == "cursor_time") { reply(socket, c->setAutomationCursor(p.value("time").toDouble(), p.value("index").toInt(1)) ? ok() : fail("rejected", "No active plot or valid original sample for cursor")); return true; }
    if (command == "export_logical") {
        const auto path = p.value("directory").toString(), format = p.value("format").toString();
        reply(socket, c->exportLogical(path, format) ? ok(QJsonObject{{"accepted", true}, {"completed", false}, {"directory", path}, {"data", "data." + format}, {"manifest", "manifest.json"}}) : fail("rejected", "Logical export could not start")); return true;
    }
    if (command == "mapping_preview") {
        const auto result = c->mappingPreview(p.value("template").toObject().toVariantMap());
        reply(socket, result.contains("error") ? fail("invalid_template", result.value("error").toString()) : ok(QJsonObject::fromVariantMap(result))); return true;
    }
    if (command == "template_save" || command == "template_read") {
        QJsonObject definition;
        if (command == "template_save") definition = p.value("template").toObject();
        else {
            QFile file(p.value("path").toString());
            if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) { reply(socket, fail("template_read_failed", "Template is unreadable or exceeds 64 KiB")); return true; }
            QJsonParseError parseError; const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) { reply(socket, fail("invalid_template", "Expected a JSON template object")); return true; }
            definition = document.object();
        }
        const auto schema = commandSpec("mapping_preview").value("inputSchema").toObject().value("properties").toObject().value("template").toObject();
        const auto error = validateCommandValue(definition, schema, "template");
        if (!error.isEmpty()) { reply(socket, fail("invalid_template", error)); return true; }
        if (command == "template_save") {
            QSaveFile file(p.value("path").toString()); const auto bytes = QJsonDocument(definition).toJson();
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { reply(socket, fail("template_save_failed", file.errorString())); return true; }
        }
        reply(socket, ok(definition)); return true;
    }
    if (command == "aircraft_setup" || command == "mapping_apply") {
        if (p.value("generation").toString() != generation()) { reply(socket, fail("stale_generation", "Discover sources again")); return true; }
        auto config = p.value("config").toObject(); auto bindings = p.value("bindings").toObject();
        if (command == "mapping_apply") {
            const auto preview = c->mappingPreview(p.value("template").toObject().toVariantMap());
            if (!preview.value("ready").toBool()) { reply(socket, fail("ambiguous_mapping", "Mapping is incomplete, ambiguous or has an incompatible unit; inspect mapping_preview")); return true; }
            config = QJsonObject::fromVariantMap(preview.value("config").toMap()); bindings = QJsonObject::fromVariantMap(preview.value("bindings").toMap());
        }
        if (p.contains("plot") && p.value("plot").toInt() >= c->plotRows() * c->plotColumns()) { reply(socket, fail("invalid_params", "Plot is outside the current layout")); return true; }
        if (!p.contains("object") && !p.contains("name")) { reply(socket, fail("invalid_params", "New aircraft requires a name")); return true; }
        const auto result = c->setupAircraft(p.value("object").toString(), p.value("name").toString(), config.toVariantMap(), bindings.toVariantMap());
        if (result.contains("error")) { reply(socket, fail("rejected", result.value("error").toString())); return true; }
        const auto objectId = result.value("object").toString();
        if (p.contains("plot") && !c->showObjectTrajectory(objectId, p.value("plot").toInt())) { reply(socket, fail("trajectory_failed", c->status())); return true; }
        if (p.contains("cursorTime") && !c->setAutomationCursor(p.value("cursorTime").toDouble())) { reply(socket, fail("cursor_failed", "Cannot attach cursor; object bindings were applied")); return true; }
        reply(socket, ok(QJsonObject::fromVariantMap(result))); return true;
    }
    if (command == "merge_plan") {
        const auto id = p.value("id").toString();
        reply(socket, m_plans.contains(id) ? ok(m_plans.value(id)) : fail("unknown_plan", "Plan is not retained by this instance")); return true;
    }
    if (command == "merge_execute") {
        const auto id = p.value("id").toString();
        if (!m_plans.contains(id)) { reply(socket, fail("unknown_plan", "Plan is not retained by this instance")); return true; }
        auto &plan = m_plans[id];
        if (plan.contains("executedTask")) {
            const auto previous = jobValue(plan.value("executedTask").toString());
            if (previous.value("state") == "failed" || previous.value("state") == "cancelled") { reply(socket, fail("previous_execution_failed", "This plan was already attempted; inspect its task and retry the existing rules")); return true; }
            reply(socket, ok(plan.value("executionResult"))); return true;
        }
        if (plan.value("generation").toString() != generation()) { reply(socket, fail("stale_plan", "Data changed after preview; create a new plan")); return true; }
        if (!plan.value("ready").toBool()) { reply(socket, fail("plan_conflicts", "Plan contains conflicts or invalid groups; inspect merge_plan")); return true; }
        QVariantList groups;
        for (const auto &value : plan.value("groups").toArray()) {
            const auto group = value.toObject(); if (group.value("existing").toBool()) continue;
            groups.append(QVariantMap{{"name", group.value("name").toString()}, {"signals", group.value("signals").toArray().toVariantList()}});
        }
        QVariantMap result;
        if (groups.isEmpty()) { result = {{"outputs", QVariantList{}}, {"accepted", true}, {"completed", true}, {"existingGroups", plan.value("existingGroups").toInt()}}; m_jobs[jobId].watch.clear(); }
        else result = c->mergeSignals(groups);
        if (result.contains("error")) { reply(socket, fail("rejected", result.value("error").toString())); return true; }
        plan["executedTask"] = jobId; plan["executionResult"] = QJsonObject::fromVariantMap(result);
        reply(socket, ok(QJsonObject::fromVariantMap(result))); return true;
    }
    if (command == "merge_preview") {
        if (m_plans.size() >= 32) { reply(socket, fail("plan_capacity", "This instance retains 32 merge plans")); return true; }
        auto plan = QJsonObject::fromVariantMap(c->mergePlan(p.value("policy").toObject().toVariantMap()));
        const auto id = QUuid::createUuid().toString(QUuid::Id128); plan["id"] = id; plan["generation"] = generation();
        QVector<int> ids; for (int row = 0; row < c->signalCount(); ++row) if (!c->isDerivedSignal(row)) ids.append(row);
        const auto snapshot = c->automationSnapshot(ids); const int outputStart = c->signalCount();
        const auto cancelled = m_jobs[jobId].cancelled; m_analyzing = true;
        m_plans[id] = plan;
        m_workers.start(QRunnable::create([this, plan, id, jobId, snapshot, outputStart, cancelled]() mutable {
            try {
            QVector<DataObject> objects; QJsonArray conflicts; int output = outputStart; QHash<QString, int> ruleGroups;
            auto groups = plan.value("groups").toArray();
            for (int i = 0; i < groups.size(); ++i) {
                const auto group = groups[i].toObject();
                if (group.value("existing").toBool()) {
                    if (!group.value("existingRule").toObject().value("error").toString().isEmpty()) conflicts.append(QJsonObject{{"group", i}, {"error", group.value("existingRule").toObject().value("error")}});
                    continue;
                }
                if (!group.value("eligible").toBool()) { conflicts.append(QJsonObject{{"group", i}, {"error", "Group exceeds 256 input fields"}}); continue; }
                DataObject object; object.id = "preview-object-" + QString::number(i); object.name = "preview";
                ObjectRule rule; rule.id = "preview-rule-" + QString::number(i); rule.name = group.value("name").toString(); rule.operation = "merge";
                int field = 0;
                for (const auto &source : group.value("signals").toArray()) { const auto fieldId = object.id + '-' + QString::number(field++); object.fields.append({fieldId, fieldId, "scalar", source.toInt()}); rule.inputs.append(fieldId); }
                rule.outputs.append({rule.id + "-output", rule.name, output++}); object.rules.append(rule); objects.append(object); ruleGroups[rule.id] = i;
            }
            const auto evaluation = evaluateObjects(objects, snapshot, *cancelled);
            for (auto it = evaluation.errors.cbegin(); it != evaluation.errors.cend(); ++it) conflicts.append(QJsonObject{{"group", ruleGroups.value(it.key())}, {"error", it.value()}});
            int at = 0;
            for (const auto &object : objects) {
                const int i = ruleGroups.value(object.rules.first().id); auto group = groups[i].toObject();
                if (at < evaluation.series.size()) group["outputSamples"] = double(evaluation.series[at++]->sampleCount());
                groups[i] = group;
            }
            plan["groups"] = groups; plan["conflicts"] = conflicts; plan["ready"] = conflicts.isEmpty() && !cancelled->load();
            plan["timelineBuilds"] = double(evaluation.mergeTimelineBuilds); plan["timelineHits"] = double(evaluation.mergeTimelineHits);
            QMetaObject::invokeMethod(this, [this, plan, id, jobId, cancelled] {
                m_analyzing = false; m_plans[id] = plan;
                completeJob(jobId, plan, {}, cancelled->load());
            }, Qt::QueuedConnection);
            } catch (const std::exception &error) {
                const auto message = QString::fromUtf8(error.what());
                QMetaObject::invokeMethod(this, [this, id, jobId, message] {
                    m_analyzing = false; m_plans[id]["ready"] = false;
                    completeJob(jobId, m_plans[id], message);
                }, Qt::QueuedConnection);
            }
        }));
        reply(socket, ok(QJsonObject{{"accepted", true}, {"completed", false}, {"plan", id}})); return true;
    }
    return false;
}
