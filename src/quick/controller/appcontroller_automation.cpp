#include "appcontroller.h"
#include "items/plotitem.h"
#include "io/dataloadworker.h"
#include "io/dataexportworker.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QUuid>
#include <QDir>
#include <cmath>
#include <map>

namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString mergeKey(QVector<int> rows) { std::sort(rows.begin(), rows.end()); QStringList list; for (int r : rows) list.append(QString::number(r)); return list.join(','); }
}

QVariantMap AppController::mergePlan(const QVariantMap &policy) const
{
    const auto aliases = policy.value("aliases").toMap();
    const bool caseSensitive = policy.value("caseSensitive", true).toBool();
    const QString file = policy.value("sourceFile").toString();
    std::map<std::pair<QString, QString>, QVariantList> buckets;
    QHash<QString, QVariantMap> existing;
    for (const auto &object : m_objects) {
        QHash<QString, int> fields; for (const auto &field : object.fields) fields[field.id] = field.series;
        for (const auto &rule : object.rules) if (rule.operation == "merge") {
            QVector<int> rows; for (const auto &input : rule.inputs) if (fields.contains(input)) rows.append(fields[input]);
            if (rows.size() == rule.inputs.size()) existing[mergeKey(rows)] = {{"object", object.id}, {"rule", rule.id}, {"error", m_objectErrors.value(rule.id)}};
        }
    }
    for (int row = 0; row < signalCount(); ++row) {
        if (isObjectOutput(row)) continue;
        const auto snap = automationSnapshot({row}); if (snap.series.isEmpty()) continue;
        const auto &s = snap.series.first(); if (!file.isEmpty() && s->sourceFile != file) continue;
        if (policy.contains("tables") && !policy.value("tables").toStringList().contains(s->sourceTableName)) continue;
        QString name = m_signals->originalNameAt(row); name = aliases.value(name, name).toString();
        buckets[{s->sourceFile, caseSensitive ? name : name.toCaseFolded()}].append(row);
    }
    QVariantList groups; int singles = 0, skipped = 0; double estimated = 0;
    for (const auto &entry : buckets) {
        if (entry.second.size() < 2) { ++singles; continue; }
        QVector<int> rows; double samples = 0; QStringList tables;
        for (const auto &v : entry.second) { const int r = v.toInt(); rows.append(r); samples += automationSnapshot({r}).series.first()->sampleCount(); tables.append(m_signals->groupAt(r)); }
        const auto found = existing.value(mergeKey(rows));
        QVariantMap group{{"name", aliases.value(m_signals->originalNameAt(rows.first()), m_signals->originalNameAt(rows.first()))},
            {"signals", entry.second}, {"sourceFile", entry.first.first}, {"tables", tables}, {"inputSamples", samples},
            {"existing", !found.isEmpty()}, {"existingRule", found}, {"eligible", rows.size() <= 256}};
        if (!found.isEmpty()) ++skipped; else estimated += samples * 8;
        groups.append(group);
    }
    return {{"groups", groups}, {"duplicateGroups", groups.size()}, {"singletonSignals", singles},
        {"existingGroups", skipped}, {"estimatedValueBytesUpperBound", estimated}, {"policy", policy}};
}

QVariantMap AppController::setupAircraft(const QString &objectId, const QString &name, const QVariantMap &config, const QVariantMap &bindings)
{
    if (loading() || exporting() || deriving() || sessionInteractionBlocked()) return {{"error", "Controller busy"}};
    auto next = m_objects; int index = -1;
    for (int i = 0; i < next.size(); ++i) if (next[i].id == objectId) index = i;
    if (!objectId.isEmpty() && index < 0) return {{"error", "Unknown aircraft object"}};
    if (index < 0) { DataObject object; object.id = id(); object.type = "aircraft"; object.name = name; next.append(object); index = next.size() - 1; }
    auto &object = next[index];
    if (object.type != "aircraft") return {{"error", "Object is not aircraft"}};
    if (!name.isEmpty()) object.name = name;
    object.geographic = config.value("geographic").toBool(); object.attitude.mode = config.value("attitudeMode").toInt();
    object.attitude.radians = config.value("radians").toBool(); object.attitude.order = config.value("order").toInt();
    object.attitude.scalarLast = config.value("scalarLast").toBool(); object.attitude.navigationToBody = config.value("navigationToBody").toBool();
    if (!ensureAircraftFields(object)) return {{"error", "Field limit exceeded"}};
    const auto roles = activeAircraftFieldRoles(object);
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        if (!roles.contains(it.key())) return {{"error", "Binding role is not enabled in aircraft configuration"}};
        bool ok = false; const double source = it.value().toDouble(&ok);
        if (!ok || !std::isfinite(source) || std::floor(source) != source || source < -1 || source >= signalCount() || (source >= 0 && isObjectOutput(int(source))))
            return {{"error", "Invalid raw signal binding"}};
        for (auto &field : object.fields) if (field.role == it.key()) field.series = int(source);
    }
    QString error; if (!validateObjects(next, signalCount(), &error)) return {{"error", error}};
    const auto savedId = object.id;
    m_objects = std::move(next); markSessionModified(); scheduleObjectEvaluation();
    return {{"object", savedId}, {"check", aircraftCheck(savedId)}};
}

QVariantMap AppController::aircraftCheck(const QString &objectId) const
{
    for (const auto &object : m_objects) if (object.id == objectId && object.type == "aircraft") {
        QVariantList fields, views; QStringList missing, optionalMissing; int positionBindings = 0; bool attitudeBindings = true;
        const auto active = activeAircraftFieldRoles(object);
        for (const auto &field : object.fields) if (active.contains(field.role)) {
            const auto snap = automationSnapshot({field.series});
            const bool ready = !snap.series.isEmpty() && snap.series.first()->sampleCount() > 0;
            const bool position = QStringList{"latitude", "longitude", "height", "x", "y", "z"}.contains(field.role);
            if (position && ready) ++positionBindings;
            if (!position && !ready) attitudeBindings = false;
            if (!ready) { if (field.role == "height" || (!object.geographic && position)) optionalMissing.append(field.role); else missing.append(field.role); }
            const QString unit = position ? (field.role == "latitude" || field.role == "longitude" ? "deg" : "m") : object.attitude.mode == 2 ? "unitless" : object.attitude.radians ? "rad" : "deg";
            fields.append(QVariantMap{{"field", field.id}, {"role", field.role}, {"signal", field.series}, {"ready", ready}, {"name", field.name}, {"unit", unit}, {"unitEvidence", "aircraft configuration"}});
        }
        bool visible = false;
        for (int i = 0; i < plotRows() * plotColumns(); ++i) {
            const auto state = trajectoryState(i);
            if (state.value("objectId").toString() != objectId) continue;
            auto view = trajectoryRenderState(i); view["plot"] = i;
            view["cursorMode"] = plotAt(i) ? plotAt(i)->cursorMode() : 0;
            const bool shown = view.value("ready").toBool() && object.attitude.mode > 0
                && view.value("attitudeStatus").toString().isEmpty() && view.value("cursorMode").toInt() > 0;
            visible |= shown; view["attitudeVisible"] = shown; views.append(view);
        }
        const bool positionReady = object.geographic ? !missing.contains("latitude") && !missing.contains("longitude") : positionBindings >= 2;
        return {{"object", objectId}, {"fields", fields}, {"missingRoles", missing}, {"optionalMissingRoles", optionalMissing}, {"positionReady", positionReady}, {"attitudeBindingsReady", attitudeBindings}, {"bindingsReady", positionReady && attitudeBindings},
            {"attitudeEnabled", object.attitude.mode > 0}, {"attitudeVisible", visible}, {"views", views},
            {"convention", QVariantMap{{"geographic", object.geographic}, {"attitudeMode", object.attitude.mode},
                {"radians", object.attitude.radians}, {"order", object.attitude.order}, {"navigationToBody", object.attitude.navigationToBody}, {"scalarLast", object.attitude.scalarLast}}}};
    }
    return {{"error", "Unknown aircraft object"}};
}

QVariantMap AppController::mappingPreview(const QVariantMap &definition) const
{
    QVariantMap bindings; QVariantList fields; bool ready = true;
    const auto roles = definition.value("roles").toMap();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const auto spec = it.value().toMap(); const auto aliases = spec.value("aliases").toStringList();
        const QRegularExpression pattern(spec.value("pattern").toString()); QVariantList candidates;
        if (!pattern.isValid()) return {{"error", "Invalid alias regular expression"}};
        const QString unit = spec.value("unit").toString();
        const QString expected = QStringList{"qw", "qx", "qy", "qz"}.contains(it.key()) ? "unitless" : (it.key() == "latitude" || it.key() == "longitude") ? "deg"
            : (it.key() == "height" || QStringList{"x", "y", "z"}.contains(it.key())) ? "m"
            : definition.value("config").toMap().value("radians").toBool() ? "rad" : "deg";
        const bool unitValid = unit == expected;
        for (int row = 0; row < signalCount(); ++row) {
            if (isObjectOutput(row)) continue;
            const auto snap = automationSnapshot({row}); if (snap.series.isEmpty()) continue;
            const auto &s = snap.series.first();
            if (spec.contains("sourceFile") && s->sourceFile != spec.value("sourceFile").toString()) continue;
            if (spec.contains("table") && s->sourceTableName != spec.value("table").toString()) continue;
            const auto name = originalSignalName(row);
            if (aliases.contains(name) || (!spec.value("pattern").toString().isEmpty() && pattern.match(name).hasMatch()))
                candidates.append(QVariantMap{{"signal", row}, {"name", name}, {"sourceFile", s->sourceFile}, {"table", s->sourceTableName}});
        }
        const bool resolved = candidates.size() == 1 && unitValid;
        if (resolved) bindings[it.key()] = candidates.first().toMap().value("signal"); else ready = false;
        fields.append(QVariantMap{{"role", it.key()}, {"candidates", candidates}, {"resolved", resolved}, {"unit", unit}, {"expectedUnit", expected}, {"unitValid", unitValid}, {"unitEvidence", "template declaration; samples are not converted"}});
    }
    const auto config = definition.value("config").toMap(); DataObject example; example.geographic = config.value("geographic").toBool();
    example.attitude.mode = config.value("attitudeMode").toInt(); example.attitude.scalarLast = config.value("scalarLast").toBool();
    const auto activeRoles = activeAircraftFieldRoles(example);
    for (const auto &role : activeRoles) if (role != "height" && role != "x" && role != "y" && role != "z" && !roles.contains(role)) ready = false;
    if (!example.geographic) { int spatial = 0; for (const auto &role : QStringList{"x", "y", "z"}) spatial += bindings.contains(role); if (spatial < 2) ready = false; }
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) if (!activeRoles.contains(it.key())) ready = false;
    return {{"ready", ready && !roles.isEmpty()}, {"fields", fields}, {"bindings", bindings}, {"config", config}};
}

QVector<int> AppController::logicalSignalRows() const
{
    QSet<int> replaced, invalidOutputs;
    for (const auto &object : m_objects) {
        QHash<QString, int> fields; for (const auto &field : object.fields) fields[field.id] = field.series;
        for (const auto &rule : object.rules) if (rule.operation == "merge") {
            bool good = m_objectErrors.value(rule.id).isEmpty();
            for (const auto &output : rule.outputs) { const auto snap = automationSnapshot({output.series}); good &= !snap.series.isEmpty() && snap.series.first()->sampleCount() > 0; }
            if (good) for (const auto &input : rule.inputs) { if (fields.contains(input)) replaced.insert(fields[input]); }
            else for (const auto &output : rule.outputs) invalidOutputs.insert(output.series);
        }
    }
    QVector<int> rows; for (int row = 0; row < signalCount(); ++row) if (!replaced.contains(row) && !invalidOutputs.contains(row)) rows.append(row);
    return rows;
}

void AppController::setLogicalSignalView(bool enabled) { m_logicalSignalView = enabled; refreshLogicalSignalView(); emit plotBindingsChanged(); }
void AppController::setSignalBrowserSection(int section)
{
    if (section < -1 || section > 1 || m_signalBrowserSection == section) return;
    m_signalBrowserSection = section;
    refreshLogicalSignalView();
    emit signalBrowserSectionChanged();
}
void AppController::refreshLogicalSignalView()
{
    if (m_signalBrowserSection < 0 && !m_logicalSignalView) { m_signals->setVisibleSources(std::nullopt); return; }
    QSet<int> derived;
    for (const auto &definition : m_objects) for (const auto &rule : definition.rules)
        for (const auto &output : rule.outputs) derived.insert(output.series);
    QSet<int> visible;
    const auto candidates = m_logicalSignalView && m_signalBrowserSection != 0 ? logicalSignalRows() : QVector<int>{};
    if (m_logicalSignalView && m_signalBrowserSection != 0) {
        for (int row : candidates) if (m_signalBrowserSection < 0 || derived.contains(row)) visible.insert(row);
    } else for (int row = 0; row < signalCount(); ++row)
        if ((m_signalBrowserSection == 1) == derived.contains(row)) visible.insert(row);
    m_signals->setVisibleSources(visible);
}

QVariantMap AppController::ruleResults(const QString &search, const QString &state, int offset, int limit) const
{
    QVariantList rows; int succeeded = 0, failed = 0, pending = 0, total = 0;
    for (const auto &object : dataObjects()) for (const auto &value : object.toMap().value("rules").toList()) {
        auto rule = value.toMap(); bool hasValues = false;
        for (const auto &output : rule.value("outputs").toList()) { const auto snap = automationSnapshot({output.toMap().value("series").toInt()}); hasValues |= !snap.series.isEmpty() && snap.series.first()->sampleCount() > 0; }
        const bool warning = hasValues && !rule.value("error").toString().isEmpty();
        rule["warning"] = warning;
        QString phase = deriving() ? "pending" : rule.value("error").toString().isEmpty() || warning ? "succeeded" : "failed";
        if (phase == "pending") ++pending; else if (phase == "failed") ++failed; else ++succeeded;
        if ((!search.isEmpty() && !rule.value("name").toString().contains(search, Qt::CaseInsensitive)) || (!state.isEmpty() && phase != state)) continue;
        if (total >= offset && rows.size() < limit) { rule["state"] = phase; rule["object"] = object.toMap().value("id"); rows.append(rule); }
        ++total;
    }
    return {{"rules", rows}, {"total", total}, {"nextOffset", offset + rows.size() < total ? QVariant(offset + rows.size()) : QVariant()},
        {"summary", QVariantMap{{"succeeded", succeeded}, {"failed", failed}, {"pending", pending}}}};
}

bool AppController::exportLogical(const QString &directory, const QString &format)
{
    if (loading() || exporting() || deriving() || restoringSession() || !QStringList{"mat", "xlsx"}.contains(format)) return false;
    QVector<DataExportTable> tables; if (!collectExportTables(LogicalResults, &tables)) return false;
    QVariantList columns;
    for (int row : logicalSignalRows()) {
        const auto snap = automationSnapshot({row}); if (snap.series.isEmpty()) continue;
        const auto &s = snap.series.first();
        columns.append(QVariantMap{{"signal", row}, {"name", signalName(row)}, {"sourceFile", s->sourceFile}, {"table", s->sourceTable}, {"column", s->sourceColumn},
            {"samples", double(s->sampleCount())}, {"timeOffset", s->timeOffset}});
    }
    QVariantList sources;
    for (int row = 0; row < signalCount(); ++row) if (!isObjectOutput(row)) {
        const auto snap = automationSnapshot({row}); if (snap.series.isEmpty()) continue;
        const auto &s = snap.series.first();
        sources.append(QVariantMap{{"signal", row}, {"name", originalSignalName(row)}, {"sourceFile", s->sourceFile},
            {"table", s->sourceTable}, {"tableName", s->sourceTableName}, {"column", s->sourceColumn},
            {"samples", double(s->sampleCount())}, {"timeOffset", s->timeOffset}});
    }
    const auto manifest = QJsonDocument(QJsonObject::fromVariantMap({{"format", "DataInspectorLogicalExport"}, {"version", 1}, {"columns", columns},
        {"sources", sources}, {"objects", objectsToJson(m_objects).toVariantList()}, {"rules", ruleResults()},
        {"rawSignalCount", sources.size()}, {"workspaceSignalCount", signalCount()}, {"logicalSignalCount", columns.size()}})).toJson();
    beginExport(format.toUpper());
    QMetaObject::invokeMethod(m_exporter, [worker = m_exporter, directory, format, tables, manifest] { worker->exportLogicalPackage(directory, format, tables, manifest); }, Qt::QueuedConnection);
    return true;
}

void AppController::cancelAutomation(const QString &kind)
{
    if (kind == "load" || kind == "restore_session") {
        m_cancelLoad = true; m_loadQueue.clear(); m_pendingPaths.clear(); if (m_loader) m_loader->requestCancel();
    } else if (kind == "derive") {
        cancelObjectEvaluation();
        for (const auto &object : m_objects) for (const auto &rule : object.rules)
            if (!m_objectEvaluationStates.contains(rule.id)) m_objectErrors[rule.id] = QStringLiteral("计算已取消，可重试");
        emit objectsChanged();
    }
    else cancelExport();
}

void AppController::retryObjectRules()
{
    if (loading() || exporting() || deriving() || restoringSession()) return;
    m_objectEvaluationStates.clear(); m_objectErrors.clear(); scheduleObjectEvaluation();
}

bool AppController::setAutomationCursor(double time, int index)
{
    auto *plot = plotAt(activePlotIndex()); if (!plot || !std::isfinite(time) || index < 1 || index > 2) return false;
    plot->setCursorMode(index); return plot->editCursorTime(time, index);
}
