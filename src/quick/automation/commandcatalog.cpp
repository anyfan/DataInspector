#include "commandcatalog.h"
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <cmath>

namespace {
using Schema = QJsonObject;
Schema text(int maximum = 256) { return {{"type", "string"}, {"minLength", 1}, {"maxLength", maximum}}; }
Schema integer(double lo = 0, double hi = 2147483647) { return {{"type", "integer"}, {"minimum", lo}, {"maximum", hi}}; }
Schema number() { return {{"type", "number"}}; }
Schema flag() { return {{"type", "boolean"}}; }
Schema enumeration(const QStringList &choices) { Schema s = text(); s["enum"] = QJsonArray::fromStringList(choices); return s; }
Schema array(const Schema &items, int lo = 0, int hi = 32768) { return {{"type", "array"}, {"items", items}, {"minItems", lo}, {"maxItems", hi}}; }
Schema object(std::initializer_list<std::pair<QString, Schema>> fields)
{
    Schema properties; QJsonArray required;
    for (auto field : fields) { const bool optional = field.first.endsWith('?'); if (optional) field.first.chop(1); else required.append(field.first); properties[field.first] = field.second; }
    return {{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
}
Schema openObject() { return {{"type", "object"}}; }
Schema path() { auto s = text(32768); s["format"] = "absolute-path"; return s; }
Schema config() { return object({{"geographic", flag()}, {"attitudeMode", integer(0, 2)}, {"radians", flag()}, {"order", integer(0, 1)}, {"scalarLast", flag()}, {"navigationToBody", flag()}}); }
Schema bindings() {
    Schema s = object({}); Schema properties;
    for (const auto &role : QStringList{"latitude", "longitude", "height", "x", "y", "z", "roll", "pitch", "yaw", "qw", "qx", "qy", "qz"}) properties[role] = integer(-1);
    s["properties"] = properties; return s;
}
Schema policy() { auto aliases = openObject(); aliases["additionalProperties"] = text(); return object({{"sourceFile?", path()}, {"tables?", array(text(), 1, 256)}, {"caseSensitive?", flag()}, {"aliases?", aliases}}); }
Schema mapping() {
    auto roles = bindings(); Schema properties;
    const auto names = bindings().value("properties").toObject().keys();
    for (const auto &role : names) properties[role] = object({{"aliases?", array(text(), 0, 256)}, {"pattern?", text(1024)}, {"unit", enumeration({"deg", "rad", "m", "unitless"})}, {"sourceFile?", path()}, {"table?", text()}});
    roles["properties"] = properties;
    return object({{"version", integer(1, 1)}, {"config", config()}, {"roles", roles}});
}
Schema aircraftResult() {
    return object({{"object", text()}, {"positionReady", flag()}, {"attitudeBindingsReady", flag()},
        {"bindingsReady", flag()}, {"attitudeEnabled", flag()}, {"attitudeVisible", flag()},
        {"missingRoles", array(text())}, {"optionalMissingRoles", array(text())}, {"fields", array(openObject())},
        {"views", array(openObject())}, {"convention", config()}});
}
Schema planResult() {
    auto result = object({{"id", text(128)}, {"generation", text(128)}, {"groups", array(openObject())},
        {"duplicateGroups", integer()}, {"singletonSignals", integer()}, {"existingGroups", integer()},
        {"estimatedValueBytesUpperBound", number()}, {"ready?", flag()}, {"conflicts?", array(openObject())},
        {"timelineBuilds?", integer()}, {"timelineHits?", integer()}, {"policy", policy()}});
    result["additionalProperties"] = true; return result;
}
}

QJsonArray automationCatalog()
{
    static const QJsonArray specs = [] {
        QJsonArray list;
        const auto add = [&](const QString &name, const QString &description, const Schema &input, bool mutating = false, bool asynchronous = false, Schema output = {}) {
            auto parameters = input; auto properties = parameters.value("properties").toObject();
            for (const auto &key : QStringList{"offset", "limit", "logical", "index"}) if (properties.contains(key)) {
                auto item = properties[key].toObject();
                item["default"] = key == "offset" ? QJsonValue(0) : key == "logical" ? QJsonValue(false) : key == "index" ? QJsonValue(1)
                    : QJsonValue(name == "objects" || name == "jobs" ? 20 : name == "samples" ? 1000 : 100);
                properties[key] = item;
            }
            parameters["properties"] = properties;
            if (output.isEmpty()) {
                Schema result;
                if (name == "signals") result = {{"generation", text(128)}, {"signals", array(openObject())}, {"total", integer()}, {"nextOffset", Schema{{"anyOf", QJsonArray{integer(), Schema{{"type", "null"}}}}}}};
                else if (name == "status") result = {{"cli", openObject()}, {"generation", text(128)}, {"loading", flag()}, {"restoring", flag()}, {"deriving", flag()}, {"exporting", flag()}, {"rendering", flag()}, {"signalCount", integer()}, {"fileCount", integer()}, {"plots", array(openObject())}, {"activeTask", openObject()}};
                else if (name == "samples") result = {{"generation", text(128)}, {"signal", integer()}, {"total", integer()}, {"samples", array(array(Schema{{"anyOf", QJsonArray{number(), Schema{{"type", "null"}}}}}, 2, 2))}};
                else if (name == "statistics") { result = {{"generation", text(128)}, {"signal", integer()}, {"count", integer()}, {"missing", integer()}}; for (const auto &k : {"min", "max", "mean", "stddev", "rms"}) result[k] = Schema{{"anyOf", QJsonArray{number(), Schema{{"type", "null"}}}}}; }
                else if (name == "rules") result = {{"rules", array(openObject())}, {"summary", openObject()}, {"total", integer()}};
                else if (name == "nearest") result = {{"generation", text(128)}, {"signal", integer()}, {"sample", Schema{{"anyOf", QJsonArray{array(number(), 2, 2), Schema{{"type", "null"}}}}}}};
                else if (name == "jobs") result = {{"jobs", array(openObject())}, {"total", integer()}};
                else if (name == "job_status" || name == "job_cancel") result = {{"id", text(128)}, {"command", text()}, {"state", enumeration({"running", "cancelling", "succeeded", "failed", "cancelled"})}, {"progress", integer(0, 100)}, {"cancelable", flag()}, {"result", openObject()}, {"error", openObject()}};
                else if (name == "aircraft_check") result = aircraftResult().value("properties").toObject();
                else if (name == "aircraft_setup" || name == "mapping_apply") result = {{"object", text()}, {"check", aircraftResult()}};
                else if (name == "mapping_preview") result = {{"ready", flag()}, {"fields", array(openObject())}, {"bindings", bindings()}, {"config", config()}};
                else if (name == "template_read" || name == "template_save") result = mapping().value("properties").toObject();
                else if (name == "merge_plan") result = planResult().value("properties").toObject();
                else if (name == "merge_preview") result = {{"accepted", flag()}, {"completed", flag()}, {"plan", text(128)}};
                else if (name == "merge_execute" || name == "merge_signals" || name == "rule_update" || name == "rule_create" || name == "view_template_apply") result = {{"accepted", flag()}, {"completed", flag()}, {"outputs", array(openObject())}};
                else if (name == "export_logical") result = {{"accepted", flag()}, {"completed", flag()}, {"directory", path()}, {"data", text()}, {"manifest", text()}};
                else if (name == "object_create") result = {{"object", text()}};
                else if (name == "logical_view") result = {{"enabled", flag()}, {"logicalCount", integer()}};
                else if (name == "trajectory_status") result = {{"objectId", text()}, {"attached", flag()}, {"pending", flag()}, {"ready", flag()}, {"error", Schema{{"type", "string"}}}, {"attitudeStatus", Schema{{"type", "string"}}}, {"referenceOrigin", Schema{{"type", "string"}}}};
                else if (asynchronous) result = {{"accepted", flag()}, {"completed", flag()}};
                output = {{"type", "object"}, {"properties", result}};
            }
            const auto taskSchema = object({{"id", text(128)}, {"command", text()}, {"state", enumeration({"running", "cancelling", "succeeded", "failed", "cancelled"})}, {"progress", integer(0, 100)},
                {"cancelable", flag()}, {"startedAt", text()}, {"finishedAt?", text()}, {"generation?", text(128)}, {"result", openObject()}, {"error?", openObject()}, {"plot?", integer(0, 63)}, {"attempts?", integer()}});
            Schema taskResult = output;
            if (name == "aircraft_setup" || name == "mapping_apply") taskResult = aircraftResult();
            else if (name == "merge_preview") taskResult = planResult();
            else if (name == "load") taskResult = object({{"fileCount", integer()}, {"signalCount", integer()}});
            else if (name == "merge_execute" || name == "merge_signals" || name == "rule_update" || name == "rule_create" || name == "view_template_apply" || name == "rules_retry") {
                auto properties = output.value("properties").toObject(); properties["errors"] = array(openObject()); properties["warnings"] = array(openObject()); properties["checkedRules"] = integer();
                properties["completed"] = flag(); properties["evaluationState"] = enumeration({"succeeded", "failed"}); properties["generation"] = text(128);
                properties["rules"] = array(openObject(), 0, 256); properties["rulesTruncated"] = flag();
                taskResult = {{"type", "object"}, {"properties", properties}};
            } else if (name == "object_trajectory" || name == "trajectory_view") taskResult = {{"type", "object"}, {"properties", Schema{{"attached", flag()}, {"pending", flag()}, {"ready", flag()}, {"error", Schema{{"type", "string"}}}}}};
            list.append(QJsonObject{{"command", name}, {"description", description}, {"inputSchema", parameters}, {"outputSchema", output}, {"taskSchema", taskSchema}, {"taskResultSchema", taskResult}, {"mutating", mutating}, {"asynchronous", asynchronous}});
        };
        const auto generation = text(128), signal = integer(), plot = integer(0, 63);
        const auto paged = object({{"offset?", integer()}, {"limit?", integer(1, 1000)}, {"search?", text()}, {"state?", enumeration({"pending", "succeeded", "failed"})}});
        add("help", "Machine-readable command schemas", object({}), false, false, array(openObject()));
        add("capabilities", "Protocol/API versions, catalog fingerprint and supported commands", object({}), false, false,
            object({{"protocolVersion", integer(1, 1)}, {"apiVersion", integer(3, 3)}, {"catalogHash", text(64)}, {"commands", array(text())}, {"maxRequestBytes", integer()}, {"maxEnumBatchSignals", integer()}}));
        add("status", "Application state including trajectory preparation", object({}));
        add("jobs", "Recent task states", object({{"offset?", integer()}, {"limit?", integer(1, 100)}}));
        add("job_status", "One task: progress, result, errors and cancellation", object({{"id", text(128)}}));
        add("job_cancel", "Request cooperative cancellation", object({{"id", text(128)}}));
        add("signals", "Paged signal metadata; optional logical result selection", object({{"offset?", integer()}, {"limit?", integer(1, 1000)}, {"logical?", flag()}}));
        add("objects", "Object definitions; optional pagination", object({{"offset?", integer()}, {"limit?", integer(1, 100)}}), false, false, {{"anyOf", QJsonArray{openObject(), array(openObject())}}});
        auto ruleQuery = paged; auto queryFields = ruleQuery["properties"].toObject();
        queryFields["sourceSignal"] = signal; queryFields["generation"] = generation; ruleQuery["properties"] = queryFields;
        add("rules", "Paged per-rule evaluation results; optional transitive raw source filter requires generation", ruleQuery);
        add("rules_retry", "Recompute existing definitions after cancellation or corrected data", object({}), true, true);
        add("rule_update", "Update an existing rule while preserving output identities", object({{"generation", generation}, {"object", text()}, {"rule", text()},
            {"config", object({{"name", text()}, {"operation", enumeration({"bits", "bitfield", "scale", "merge", "magnitude", "expression", "packetloss"})}, {"inputs", array(text(128), 1, 256)},
                {"expression?", text(4096)}, {"factor?", number()}, {"bias?", number()}, {"rateMode?", enumeration({"count", "cumulative", "window"})}, {"rateWindowSeconds?", number()}, {"lossMode?", enumeration({"time", "sequence", "stride", "rate"})}, {"counterStep?", number()}, {"expectedStride?", number()}, {"counterModulus?", number()}, {"counterRate?", number()}, {"counterTolerance?", number()}, {"samplePeriod?", Schema{{"type", "number"}, {"minimum", 0}}}, {"wordBits?", integer(8, 32)}, {"startBit?", integer(0, 31)}, {"bitCount?", integer(1, 32)}, {"signedField?", flag()}, {"autoWordBits?",flag()}, {"byteOrder?",enumeration({"little","big"})},
                {"outputs?",array(object({{"id?",text(128)},{"name?",text(256)},{"startBit?",integer(0,31)},{"bitCount?",integer(1,32)},{"signedField?",flag()},{"expression?",text(1024)},{"inputs?",array(text(128),2,256)}}),1,32)}})}}), true, true);
        const auto ruleConfig = list.last().toObject()["inputSchema"].toObject()["properties"].toObject()["config"].toObject();
        add("rule_create", "Create a processing rule and wait for actual evaluation", object({{"generation", generation}, {"object", text()}, {"config", ruleConfig}}), true, true);
        add("rule_remove", "Remove a rule; referenced outputs are protected", object({{"generation", generation}, {"object", text()}, {"rule", text()}}), true);
        add("rule_inputs", "Discover allowed raw and preceding rule inputs", object({{"generation", generation}, {"object", text()}, {"beforeRule?", text()}}), false, false,
            object({{"generation", generation}, {"inputs", array(openObject())}}));
        add("object_rename", "Rename an object or processing group", object({{"object", text()}, {"name", text()}}), true);
        add("object_remove", "Remove object/group and its outputs, retaining raw data", object({{"generation", generation}, {"object", text()}}), true);
        add("object_duplicate", "Duplicate object/group with new identities", object({{"generation", generation}, {"object", text()}}), true, false, object({{"object", text()}}));
        add("object_field_add", "Add an unbound scalar field", object({{"object", text()}}), true, false, object({{"field", text()}}));
        add("object_field_rename", "Rename an existing object field", object({{"object", text()}, {"field", text()}, {"name", text()}}), true);
        add("object_field_remove", "Remove an optional field; required aircraft fields are protected", object({{"object", text()}, {"field", text()}}), true);
        add("clear", "Clear data and objects; disk files remain", object({}), true);
        add("load", "Load one local file", object({{"path", path()}}), true, true);
        add("merge_signals", "Create explicit raw-source merge groups", object({{"generation", generation}, {"groups", array(object({{"name", text()}, {"signals", array(signal, 2, 256)}}), 1, 256)}}), true, true);
        add("merge_preview", "Check grouping, existing rules, conflicts and output counts without modifying data", object({{"policy?", policy()}}), false, true);
        add("merge_plan", "Read an existing checked merge plan", object({{"id", text(128)}}));
        add("merge_execute", "Execute a checked plan once", object({{"id", text(128)}}), true, true);
        add("object_create", "Create an object or independent processing configuration", object({{"name", text()}, {"type", enumeration({"general", "aircraft", "processing"})}}), true);
        add("aircraft_configure", "Configure aircraft conventions", object({{"object", text()}, {"geographic", flag()}, {"attitudeMode", integer(0, 2)}, {"radians", flag()}, {"order", integer(0, 1)}, {"scalarLast", flag()}, {"navigationToBody", flag()}}), true);
        add("object_bind_field", "Bind one existing field", object({{"generation", generation}, {"object", text()}, {"field", text()}, {"signal", integer(-1)}}), true);
        add("aircraft_setup", "Atomically create/update conventions and all bindings; optionally show trajectory and cursor", object({{"generation", generation}, {"object?", text()}, {"name?", text()}, {"config", config()}, {"bindings", bindings()}, {"plot?", plot}, {"cursorTime?", number()}}), true, true);
        add("aircraft_check", "Position/attitude/units convention/cursor/renderer completeness", object({{"object", text()}}));
        add("mapping_preview", "Resolve a configurable mapping template, expose ambiguity and unit mismatch", object({{"template", mapping()}}));
        add("mapping_apply", "Apply an unambiguous mapping to an aircraft", object({{"generation", generation}, {"template", mapping()}, {"object?", text()}, {"name?", text()}, {"plot?", plot}, {"cursorTime?", number()}}), true, true);
        add("template_save", "Save a mapping template atomically", object({{"path", path()}, {"template", mapping()}}), true);
        add("template_read", "Read and validate a mapping template", object({{"path", path()}}));
        add("object_trajectory", "Show aircraft trajectory", object({{"object", text()}, {"plot", plot}}), true, true);
        add("trajectory_status", "Trajectory preparation and render status", object({{"plot", plot}}));
        add("trajectory_view", "Set and fit isometric/top/front/side view", object({{"plot", plot}, {"preset", integer(0, 3)}}), true, true);
        add("trajectory_style", "Update an explicit track; omitted style fields retain current values", object({{"plot", plot}, {"track", text(128)}, {"name?", text()}, {"color?", text()},
            {"width?", Schema{{"type", "number"}, {"minimum", 1}, {"maximum", 12}}}, {"visible?", flag()}}), true);
        add("trajectory_select", "Select a track by stable ID from trajectory_status", object({{"plot", plot}, {"track", text(128)}}), true);
        add("trajectory_remove", "Remove a track by stable ID, preserving its source object", object({{"plot", plot}, {"track", text(128)}}), true);
        add("state_status", "Read state timeline lanes and mode", object({{"plot", plot}}), false, false, openObject());
        add("state_lane", "Add (lane=-1) or replace a state lane", object({{"generation", generation}, {"plot", plot}, {"lane", integer(-1, 63)},
            {"signal", signal}, {"name", text()}, {"visible", flag()}}), true);
        add("state_lane_remove", "Remove a state lane", object({{"plot", plot}, {"lane", integer(0, 63)}}), true);
        add("state_lane_move", "Move a state lane one row up or down", object({{"plot", plot}, {"lane", integer(0, 63)}, {"direction", integer(-1, 1)}}), true);
        add("view_mode", "Switch subplot mode while retaining bindings", object({{"plot", plot}, {"mode", enumeration({"curve", "trajectory", "state"})}}), true);
        add("plot_axes", "Read curve plot axes and normalization", object({{"plot", plot}}), false, false, openObject());
        add("plot_axes_set", "Update curve axes atomically; omitted fields retain current values", object({{"plot", plot}, {"xMinimum?", number()}, {"xMaximum?", number()},
            {"yMinimum?", number()}, {"yMaximum?", number()}, {"normalizeY?", flag()}, {"blendOverlaps?", flag()}}), true);
        add("plot_clear", "Clear curve bindings in one subplot, keeping loaded data", object({{"plot", plot}}), true);
        add("capture_window", "Capture window content as PNG", object({{"path", path()}}), true, true);
        add("samples", "Paged original samples; nonfinite values are null", object({{"generation", generation}, {"signal", signal}, {"offset?", integer()}, {"limit?", integer(1, 10000)}}), false, true);
        add("statistics", "Inclusive raw-sample interval statistics", object({{"generation", generation}, {"signal", signal}, {"start?", number()}, {"end?", number()}}), false, true);
        add("nearest", "Nearest original finite sample, no interpolation", object({{"generation", generation}, {"signal", signal}, {"time", number()}}), false, true);
        add("layout", "Set subplot grid", object({{"rows", integer(1, 8)}, {"columns", integer(1, 8)}}), true);
        add("plot_signal", "Enable/disable time-plot binding", object({{"generation", generation}, {"signal", signal}, {"plot", plot}, {"enabled", flag()}}), true);
        add("style", "Set signal pen", object({{"generation", generation}, {"signal", signal}, {"color", text()}, {"width", Schema{{"type", "number"}, {"minimum", 1}, {"maximum", 20}}}, {"style", integer(1, 5)}}), true);
        add("rename", "Rename signal display", object({{"generation", generation}, {"signal", signal}, {"name", text()}}), true);
        const auto enums = array(object({{"value", integer(-9007199254740991.0, 9007199254740991.0)}, {"text", text(256)}, {"color", text()}}), 0, 256);
        add("signal_enums", "Read shared signal enum definitions", object({{"generation", generation}, {"signal", signal}}), false, false,
            object({{"generation", generation}, {"signal", signal}, {"enums", enums}}));
        add("set_signal_enums", "Replace shared signal enums; empty array clears definitions", object({{"generation", generation}, {"signal", signal}, {"enums", enums}}), true);
        const auto enumSignal = object({{"signal", signal}, {"name", text()}, {"originalName", Schema{{"type", "string"}}}, {"enums", enums}});
        const auto batchResult = object({{"generation", generation}, {"signals", array(enumSignal, 1, 256)}, {"updated?", integer(1, 256)}});
        add("signal_enums_batch", "Read shared enums for up to 256 distinct signals", object({{"generation", generation}, {"signals", array(signal, 1, 256)}}), false, false, batchResult);
        add("set_signal_enums_batch", "Validate all enum tables before one atomic commit; return canonical applied definitions", object({{"generation", generation},
            {"updates", array(object({{"signal", signal}, {"enums", enums}}), 1, 256)}}), true, false, batchResult);
        add("signal_properties", "Atomically update signal properties; omitted fields retain current values", object({{"generation", generation}, {"signal", signal}, {"name?", text()}, {"color?", text()},
            {"width?", Schema{{"type", "number"}, {"minimum", 1}, {"maximum", 20}}}, {"style?", integer(1, 5)}, {"timeOffset?", number()}, {"enums?", enums}}), true);
        add("offset", "Set absolute signal time offset", object({{"generation", generation}, {"signal", signal}, {"seconds", number()}}), true);
        add("rename_reset", "Restore original signal name", object({{"generation", generation}, {"signal", signal}}), true);
        add("scope_offset", "Set or add time offset to a raw signal, table or file", object({{"generation", generation}, {"signal", signal},
            {"scope", enumeration({"signal", "table", "file"})}, {"group?", text(32768)}, {"seconds", number()}, {"add?", flag()}}), true);
        add("file_remove", "Unload a file and dependent outputs; disk file remains", object({{"generation", generation}, {"path", path()}}), true);
        add("cursor_mode", "Set active subplot cursor count", object({{"mode", integer(0, 2)}}), true);
        add("cursor_time", "Snap an original-sample cursor to a requested time", object({{"time", number()}, {"index?", integer(1, 2)}}), true);
        add("fit", "Fit all plots", object({}), true);
        add("logical_view", "Prefer merged results; source visibility remains reversible", object({{"enabled", flag()}}), true);
        add("export_logical", "Publish data.mat/xlsx and provenance manifest as a new directory", object({{"directory", path()}, {"format", enumeration({"mat", "xlsx"})}}), true, true);
        add("save_session", "Save workspace atomically", object({{"path", path()}}), true);
        add("restore_session", "Restore workspace", object({{"path", path()}}), true, true);
        add("view_template_save", "Export views, objects and processing definitions without data", object({{"path", path()}}), true);
        add("view_template_preview", "Preview raw-source matching and imported configuration counts", object({{"path", path()}}), false, false, openObject());
        auto sourceMapping = openObject(); sourceMapping["additionalProperties"] = signal;
        add("view_template_apply", "Import a view template and recompute its processing outputs", object({{"generation", generation}, {"path", path()}, {"mapping", sourceMapping}, {"fixedRanges?", flag()}}), true, true);
        add("view_template_undo", "Undo the last template import including created configurations", object({}), true);
        return list;
    }();
    return specs;
}

QJsonObject commandSpec(const QString &name) { for (const auto &value : automationCatalog()) if (value.toObject()["command"] == name) return value.toObject(); return {}; }

QJsonObject automationCapabilities()
{
    static const QJsonObject capabilities = [] {
        const auto catalog = automationCatalog(); QJsonArray commands;
        for (const auto &entry : catalog) commands.append(entry.toObject()["command"]);
        const auto hash = QCryptographicHash::hash(QJsonDocument(catalog).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        return QJsonObject{{"protocolVersion", 1}, {"apiVersion", 3}, {"catalogHash", QString::fromLatin1(hash)},
            {"commands", commands}, {"maxRequestBytes", 65536}, {"maxEnumBatchSignals", 256}};
    }();
    return capabilities;
}

QString validateCommandValue(const QJsonValue &value, const QJsonObject &schema, const QString &path)
{
    const QString type = schema["type"].toString();
    if ((type == "object" && !value.isObject()) || (type == "array" && !value.isArray()) || (type == "string" && !value.isString())
        || (type == "boolean" && !value.isBool()) || ((type == "integer" || type == "number") && (!value.isDouble() || !std::isfinite(value.toDouble()) || (type == "integer" && std::floor(value.toDouble()) != value.toDouble())))) return path + ": expected " + type;
    if (schema.contains("enum") && !schema["enum"].toArray().contains(value)) return path + ": value is not in enum";
    if (value.isDouble() && ((schema.contains("minimum") && value.toDouble() < schema["minimum"].toDouble()) || (schema.contains("maximum") && value.toDouble() > schema["maximum"].toDouble()))) return path + ": number out of range";
    if (value.isString()) {
        const auto s = value.toString(); if (s.size() < schema["minLength"].toInt() || (schema.contains("maxLength") && s.size() > schema["maxLength"].toInt())) return path + ": invalid string length";
        if (schema["format"] == "absolute-path" && !QFileInfo(s).isAbsolute()) return path + ": expected absolute path";
    }
    if (value.isArray()) {
        const auto list = value.toArray(); if (list.size() < schema["minItems"].toInt() || (schema.contains("maxItems") && list.size() > schema["maxItems"].toInt())) return path + ": array length out of range";
        for (int i = 0; i < list.size(); ++i) { const auto error = validateCommandValue(list[i], schema["items"].toObject(), path + '[' + QString::number(i) + ']'); if (!error.isEmpty()) return error; }
    }
    if (value.isObject()) {
        const auto o = value.toObject(), properties = schema["properties"].toObject();
        for (const auto &required : schema["required"].toArray()) if (!o.contains(required.toString())) return path + ": missing " + required.toString();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!properties.contains(it.key()) && schema.contains("additionalProperties") && schema["additionalProperties"].isBool() && !schema["additionalProperties"].toBool()) return path + ": unknown field " + it.key();
            const auto sub = properties.contains(it.key()) ? properties[it.key()].toObject() : schema["additionalProperties"].toObject();
            const auto error = validateCommandValue(it.value(), sub, path + '.' + it.key()); if (!error.isEmpty()) return error;
        }
    }
    return {};
}
