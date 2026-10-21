#include "automationserver.h"
#include "commandcatalog.h"
#include "controller/appcontroller.h"
#include <QFile>
#include <QFileInfo>
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
    if (p.contains("generation") && p.value("generation").toString() != generation()) {
        reply(socket, fail("stale_generation", "Data changed; discover current sources and definitions again")); return true;
    }
    if (p.contains("plot") && (p.value("plot").toInt() < 0 || p.value("plot").toInt() >= c->plotRows() * c->plotColumns())) {
        reply(socket, fail("invalid_params", "Plot is outside the current layout")); return true;
    }
    const auto respond = [&](bool accepted) { reply(socket, accepted ? ok() : fail("rejected", "Operation rejected; verify identities, dependencies and parameters")); };
    const auto objectId = p.value("object").toString();
    if (command == "signal_enums_batch" || command == "set_signal_enums_batch") {
        QVector<int> rows; QSet<int> seen;
        const auto entries = p.value(command == "signal_enums_batch" ? "signals" : "updates").toArray();
        for (const auto &entry : entries) {
            const int row = (command == "signal_enums_batch" ? entry : entry.toObject()["signal"]).toInt();
            if (row >= c->signalCount() || seen.contains(row)) { reply(socket, fail("invalid_params", "Each signal must exist and appear only once; batch was not changed")); return true; }
            seen.insert(row); rows.append(row);
        }
        if (command == "set_signal_enums_batch" && !c->setSignalEnumsBatch(entries.toVariantList())) {
            reply(socket, fail("rejected", c->status())); return true;
        }
        QJsonArray values;
        for (int row : rows) values.append(QJsonObject{{"signal", row}, {"name", c->signalName(row)}, {"originalName", c->originalSignalName(row)}, {"enums", QJsonArray::fromVariantList(c->signalEnums(row))}});
        QJsonObject result{{"generation", generation()}, {"signals", values}};
        if (command == "set_signal_enums_batch") result["updated"] = rows.size();
        reply(socket, ok(result)); return true;
    }
    if (command == "view_template_save") { respond(c->saveViewTemplate(p.value("path").toString())); return true; }
    if (command == "view_template_preview") {
        const auto result = c->previewViewTemplate(p.value("path").toString());
        reply(socket, result.value("error").toString().isEmpty() ? ok(QJsonObject::fromVariantMap(result)) : fail("invalid_template", result.value("error").toString())); return true;
    }
    if (command == "view_template_apply") {
        QSet<QString> previous;
        for (const auto &value : c->dataObjects()) previous.insert(value.toMap().value("id").toString());
        if (!c->applyViewTemplate(p.value("path").toString(), p.value("mapping").toObject().toVariantMap(), p.value("fixedRanges").toBool())) {
            reply(socket, fail("rejected", c->status())); return true;
        }
        QJsonArray outputs;
        for (const auto &value : c->dataObjects()) {
            const auto object = value.toMap(); if (previous.contains(object.value("id").toString())) continue;
            for (const auto &rule : object.value("rules").toList()) outputs.append(QJsonObject{{"rule", rule.toMap().value("id").toString()}});
        }
        if (outputs.isEmpty()) m_jobs[jobId].watch.clear();
        reply(socket, ok(QJsonObject{{"accepted", true}, {"completed", outputs.isEmpty()}, {"outputs", outputs}})); return true;
    }
    if (command == "view_template_undo") { respond(c->undoViewTemplate()); return true; }
    if (command == "object_rename") { respond(c->renameDataObject(objectId, p.value("name").toString())); return true; }
    if (command == "object_remove") { respond(c->removeDataObject(objectId)); return true; }
    if (command == "object_duplicate" || command == "object_field_add") {
        const auto id = command == "object_duplicate" ? c->duplicateDataObject(objectId) : c->addDataObjectField(objectId);
        reply(socket, id.isEmpty() ? fail("rejected", "Object/group or field creation rejected") : ok(QJsonObject{{command == "object_duplicate" ? "object" : "field", id}})); return true;
    }
    if (command == "object_field_rename") { respond(c->renameDataObjectField(objectId, p.value("field").toString(), p.value("name").toString())); return true; }
    if (command == "object_field_remove") { respond(c->removeObjectField(objectId, p.value("field").toString())); return true; }
    if (command == "rule_inputs") {
        bool found = false;
        for (const auto &value : c->dataObjects()) {
            const auto object = value.toMap();
            if (object.value("id").toString() != objectId || object.value("type").toString() != "processing") continue;
            found = !p.contains("beforeRule");
            for (const auto &rule : object.value("rules").toList()) found |= rule.toMap().value("id").toString() == p.value("beforeRule").toString();
        }
        reply(socket, found ? ok(QJsonObject{{"generation", generation()}, {"inputs", QJsonArray::fromVariantList(c->processingRuleInputs(objectId, p.value("beforeRule").toString()))}})
            : fail("rejected", "Unknown processing group or rule")); return true;
    }
    if (command == "rule_create") {
        const auto id = c->addObjectRule(objectId, p.value("config").toObject().toVariantMap());
        reply(socket, id.isEmpty() ? fail("rejected", "Rule creation rejected; verify processing group, inputs and configuration")
            : ok(QJsonObject{{"accepted", true}, {"completed", false}, {"outputs", QJsonArray{QJsonObject{{"rule", id}}}}})); return true;
    }
    if (command == "rule_remove") { respond(c->removeObjectRule(objectId, p.value("rule").toString())); return true; }
    if (command == "rename_reset") { respond(c->resetSignalName(p.value("signal").toInt())); return true; }
    if (command == "scope_offset") {
        const auto scopeName = p.value("scope").toString();
        const int scope = scopeName == "signal" ? 0 : scopeName == "table" ? 1 : 2;
        const int row = p.value("signal").toInt();
        if (row >= c->signalCount() || c->isDerivedSignal(row)) { reply(socket, fail("invalid_params", "A valid raw signal is required to identify the scope")); return true; }
        const auto sourceGroup = c->signalModel()->groupAt(row);
        const auto group = p.contains("group") ? p.value("group").toString() : scope == 2 ? sourceGroup.section('/', 0, 0) : sourceGroup;
        respond(p.value("add").toBool() ? c->addTimeOffset(scope, row, group, p.value("seconds").toDouble())
            : c->setTimeOffset(scope, row, group, p.value("seconds").toDouble())); return true;
    }
    if (command == "file_remove") {
        QVector<int> rows;
        for (int row = 0; row < c->signalCount(); ++row) if (!c->isDerivedSignal(row)) rows.append(row);
        const auto snapshot = c->automationSnapshot(rows); QString fileGroup;
        for (const auto &series : snapshot.series) if (QFileInfo(series->sourceFile) == QFileInfo(p.value("path").toString())) {
            fileGroup = c->signalModel()->groupAt(series->id).section('/', 0, 0); break;
        }
        respond(!fileGroup.isEmpty() && c->removeFile(fileGroup)); return true;
    }
    const int plot = p.value("plot").toInt();
    if (command == "trajectory_style" || command == "trajectory_select" || command == "trajectory_remove") {
        const auto tracks = c->trajectoryState(plot).value("tracks").toList();
        int track = -1;
        for (int i = 0; i < tracks.size(); ++i) if (tracks[i].toMap().value("id").toString() == p.value("track").toString()) track = i;
        if (track < 0) { reply(socket, fail("rejected", "Unknown track ID; read trajectory_status again")); return true; }
        if (command == "trajectory_remove") { respond(c->removeTrajectory(plot, track)); return true; }
        if (command == "trajectory_select") { respond(c->selectTrajectory(plot, track)); return true; }
        auto style = tracks[track].toMap();
        for (const auto &key : {"name", "color", "width", "visible"}) if (p.contains(key)) style[key] = p.value(key).toVariant();
        const auto name = style.value("name").toString(); const QColor color(style.value("color").toString());
        if (name.trimmed().isEmpty() || name.size() > 256 || name.contains(QChar::Null) || !color.isValid()) {
            reply(socket, fail("invalid_params", "Invalid track name or color")); return true;
        }
        const int previous = c->trajectoryState(plot).value("active").toInt();
        if (!c->selectTrajectory(plot, track)) { respond(false); return true; }
        const bool applied = c->styleTrajectory(plot, name, color, style.value("width").toDouble(), style.value("visible").toBool());
        if (previous != track) c->selectTrajectory(plot, previous);
        respond(applied); return true;
    }
    if (command == "state_status") { reply(socket, ok(QJsonObject::fromVariantMap(c->stateTimelineState(plot)))); return true; }
    if (command == "state_lane") {
        respond(c->configureStateLane(plot, p.value("lane").toInt(), {{"source", p.value("signal").toInt()}, {"name", p.value("name").toString()}, {"visible", p.value("visible").toBool()}})); return true;
    }
    if (command == "state_lane_remove") { respond(c->removeStateLane(plot, p.value("lane").toInt())); return true; }
    if (command == "state_lane_move") {
        if (p.value("direction").toInt() == 0) { reply(socket, fail("invalid_params", "direction must be -1 or 1")); return true; }
        respond(c->moveStateLane(plot, p.value("lane").toInt(), p.value("direction").toInt())); return true;
    }
    if (command == "view_mode") {
        const auto mode = p.value("mode").toString();
        if (mode == "trajectory") c->enterTrajectoryMode(plot);
        else if (mode == "state") c->setStateTimelineEnabled(plot, true);
        else {
            const auto state = c->trajectoryState(plot);
            if (!c->configureTrajectory(plot, false, state.value("x").toInt(), state.value("y").toInt(), state.value("z").toInt(), state.value("geographic").toBool())) { respond(false); return true; }
            c->setStateTimelineEnabled(plot, false);
        }
        reply(socket, ok()); return true;
    }
    if (command == "plot_axes" || command == "plot_axes_set") {
        auto axes = c->plotAxisProperties(plot);
        if (axes.isEmpty()) { reply(socket, fail("rejected", "An attached curve plot is required")); return true; }
        if (command == "plot_axes") { reply(socket, ok(QJsonObject::fromVariantMap(axes))); return true; }
        for (auto it = p.begin(); it != p.end(); ++it) if (it.key() != "plot") axes[it.key()] = it.value().toVariant();
        respond(c->configurePlotAxes(plot, axes)); return true;
    }
    if (command == "plot_clear") { c->clearPlotSignals(plot); reply(socket, ok()); return true; }
    if (command == "rules") {
        const int source = p.value("sourceSignal").toInt(-1);
        if (p.contains("sourceSignal") && (!p.contains("generation") || source >= c->signalCount() || c->isDerivedSignal(source))) {
            reply(socket, fail("invalid_params", "sourceSignal requires current generation and an existing raw signal")); return true;
        }
        auto result = QJsonObject::fromVariantMap(c->ruleResults(p.value("search").toString(), p.value("state").toString(), p.value("offset").toInt(), p.value("limit").toInt(100), source));
        result["generation"] = generation(); reply(socket, ok(result)); return true;
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
