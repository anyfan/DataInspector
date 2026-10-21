#include "viewtemplatedocument.h"
#include "sessiondocument.h"
#include "jsonvalidation.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
using namespace JsonValidation;
constexpr qint64 maxBytes = 16 * 1024 * 1024;
bool keys(const QJsonObject &object, const QStringList &expected)
{
    auto actual = object.keys(); auto sorted = expected;
    sorted.sort();
    return actual == sorted;
}
bool viewKeys(const QJsonObject &view)
{
    if (!keys(view, {"layout", "plots", "xRange"})
        || !keys(view.value("layout").toObject(), {"rows", "columns", "active", "solo"})) return false;
    for (const auto &value : view.value("plots").toArray()) {
        const auto plot = value.toObject();
        if (!keys(plot, {"signals", "yRange", "normalizeY", "lineWidth", "trajectory", "states", "blendOverlaps"}) || !plot["blendOverlaps"].isBool()) return false;
        const auto trajectory = plot.value("trajectory").toObject();
        if (!keys(trajectory, {"enabled", "tracks", "active", "signals", "rotation", "viewScale", "panDepth", "camera"})) return false;
        for (const auto &entry : trajectory.value("tracks").toArray()) {
            const auto track = entry.toObject();
            if (!keys(track, {"id", "objectId", "name", "visible", "geographic", "color", "width", "axes", "attitude"})
                || !keys(track.value("attitude").toObject(), {"mode", "sources", "radians", "order", "scalarLast", "navigationToBody"})) return false;
        }
    }
    return true;
}
}
QSet<int> referencedTemplateSignals(const ViewTemplateDocument &view)
{
    auto ids = referencedViewSignals(view);
    for (const auto &object : view.objects) {
        for (const auto &field : object.fields) if (field.series >= 0) ids.insert(field.series);
        for (const auto &rule : object.rules) for (const auto &output : rule.outputs) ids.insert(output.series);
    }
    return ids;
}
QSet<int> templateRawSignals(const ViewTemplateDocument &view)
{
    QSet<int> ids;
    for (auto it = view.series.cbegin(); it != view.series.cend(); ++it)
        if (it->processingId.isEmpty()) ids.insert(it.key());
    return ids;
}
ViewTemplateDocument viewTemplateFromSession(const SessionDocument &session)
{
    ViewTemplateDocument view;
    static_cast<ViewConfiguration &>(view) = session;
    view.objects = session.objects;
    for (int id : referencedTemplateSignals(view)) if (id >= 0 && id < session.series.size()) {
        const auto &signal = session.series[id];
        view.series.insert(id, {signal.objectId.isEmpty() ? QFileInfo(session.files.value(signal.file)).fileName() : QStringLiteral("对象"),
            signal.tableName, signal.originalName, signal.name, signal.table, signal.style, signal.color, signal.width, signal.enums, signal.objectId, signal.outputId});
    }
    return view;
}
QJsonObject viewTemplateToJson(const ViewTemplateDocument &view)
{
    QJsonArray signalRows;
    for (auto it = view.series.cbegin(); it != view.series.cend(); ++it) {
        const auto &s = it.value();
        signalRows.append(QJsonObject{{"id", it.key()}, {"source", s.sourceName}, {"table", s.table},
            {"tableName", s.tableName}, {"originalName", s.originalName}, {"name", s.name},
            {"color", s.color.name(QColor::HexArgb)}, {"width", s.width}, {"style", s.style}, {"enums", signalEnumsToJson(s.enums)},
            {"processingId", s.processingId}, {"outputId", s.outputId}});
    }
    QVector<DataObject> objects, processing;
    for (const auto &definition : view.objects) (isProcessingConfiguration(definition) ? processing : objects).append(definition);
    auto configuration = viewConfigurationToJson(view); auto plots = configuration["plots"].toArray();
    for (int i = 0; i < plots.size(); ++i) { auto plot = plots[i].toObject(); plot["blendOverlaps"] = view.plots[i].blendOverlaps.value_or(false); plots[i] = plot; }
    configuration["plots"] = plots;
    return {{"format", "DataInspectorViewTemplate"}, {"version", ViewTemplateDocument::version},
        {"signals", signalRows}, {"objects", objectsToJson(objects)}, {"processing", objectsToJson(processing)}, {"view", configuration}};
}
bool viewTemplateFromJson(const QJsonObject &input, ViewTemplateDocument *view, QString *error)
{
    auto fail = [&](const QString &field) { *error = QStringLiteral("视图模板格式无效：%1").arg(field); return false; };
    auto root = input;
    if (root.value("format") != QJsonValue("DataInspectorViewTemplate")) return fail("format");
    // v5 contained view-only source descriptors. Missing definitions remain empty.
    if (root.value("version") == QJsonValue(5)) {
        if (!keys(root, {"format", "version", "signals", "view"}) || !root["signals"].isArray()) return fail("v5 root");
        auto signalRows = root["signals"].toArray();
        for (int i = 0; i < signalRows.size(); ++i) {
            auto signal = signalRows[i].toObject();
            if (!keys(signal, {"id", "source", "table", "tableName", "originalName", "name", "color", "width", "style", "enums"})) return fail("v5 signals[]");
            signal["processingId"] = ""; signal["outputId"] = ""; signalRows[i] = signal;
        }
        root["signals"] = signalRows; root["objects"] = QJsonArray{}; root["processing"] = QJsonArray{}; root["version"] = ViewTemplateDocument::version;
        auto configuration = root["view"].toObject(); auto plots = configuration["plots"].toArray();
        for (int i = 0; i < plots.size(); ++i) {
            auto plot = plots[i].toObject();
            if (!keys(plot, {"signals", "yRange", "normalizeY", "lineWidth", "trajectory", "states"})) return fail("v5 view.plots");
            plot["blendOverlaps"] = false; plots[i] = plot;
        }
        configuration["plots"] = plots; root["view"] = configuration;
    }
    if (root.value("version") != QJsonValue(ViewTemplateDocument::version)
        || !keys(root, {"format", "version", "signals", "view", "objects", "processing"})
        || !root.value("signals").isArray() || root.value("signals").toArray().size() > 100000
        || !root.value("view").isObject()) return fail("version/signals/view");
    ViewTemplateDocument parsed;
    for (const auto &value : root.value("signals").toArray()) {
        if (!value.isObject()) return fail("signals[]");
        const auto obj = value.toObject(); int id = -1; QString color;
        ViewTemplateSignal signal;
        if (!keys(obj, {"id", "source", "table", "tableName", "originalName", "name", "color", "width", "style", "enums", "processingId", "outputId"})
            || !integer(obj.value("id"), 0, 99999, &id) || parsed.series.contains(id)
            || !integer(obj.value("table"), 0, 100000, &signal.table)
            || !text(obj.value("source"), &signal.sourceName, true)
            || !text(obj.value("tableName"), &signal.tableName, true)
            || !text(obj.value("originalName"), &signal.originalName, true)
            || !text(obj.value("name"), &signal.name)
            || !text(obj.value("color"), &color) || !(signal.color = QColor(color)).isValid()
            || !number(obj.value("width"), &signal.width) || signal.width < 1 || signal.width > 20
            || !integer(obj.value("style"), 1, 5, &signal.style)
            || !signalEnumsFromJson(obj.value("enums"), &signal.enums)
            || !text(obj.value("processingId"), &signal.processingId, true)
            || !text(obj.value("outputId"), &signal.outputId, true)
            || signal.processingId.isEmpty() != signal.outputId.isEmpty()) return fail("signals[]");
        parsed.series.insert(id, signal);
    }
    const auto configuration = root.value("view").toObject();
    if (!viewKeys(configuration)) return fail("view");
    ViewConfiguration common;
    const int signalCount = parsed.series.isEmpty() ? 0 : parsed.series.lastKey() + 1;
    if (!root["objects"].isArray() || !root["processing"].isArray()
        || root["objects"].toArray().size() > 256 || root["processing"].toArray().size() > 256) return fail("objects/processing");
    auto definitions = root["objects"].toArray();
    for (const auto &value : definitions) {
        const auto object = value.toObject();
        if (!QStringList{"general", "aircraft"}.contains(object["type"].toString()) || !object["rules"].isArray() || !object["rules"].toArray().isEmpty()) return fail("objects.type/rules");
    }
    for (const auto &value : root["processing"].toArray()) {
        if (value.toObject()["type"] != QJsonValue("processing")) return fail("processing.type");
        definitions.append(value);
    }
    if (!objectsFromJson(definitions, signalCount, &parsed.objects, error)) return false;
    if (objectsToJson(parsed.objects) != definitions) return fail("definition fields");
    QSet<int> owned;
    QSet<QString> aircraft;
    for (const auto &object : parsed.objects) {
        if (object.type == "aircraft") aircraft.insert(object.id);
        for (const auto &field : object.fields) if (field.series >= 0 && (!parsed.series.contains(field.series) || !parsed.series[field.series].processingId.isEmpty())) return fail("field source");
        for (const auto &rule : object.rules) for (const auto &output : rule.outputs) {
            if (!parsed.series.contains(output.series) || parsed.series[output.series].processingId != object.id
                || parsed.series[output.series].outputId != output.id) return fail("output ownership");
            owned.insert(output.series);
        }
    }
    for (auto it = parsed.series.cbegin(); it != parsed.series.cend(); ++it)
        if (!it->processingId.isEmpty() && !owned.contains(it.key())) return fail("orphan output");
    if (!viewConfigurationFromJson(configuration, signalCount, &common, error)) return false;
    const auto plots = configuration["plots"].toArray();
    for (int i = 0; i < common.plots.size(); ++i) common.plots[i].blendOverlaps = plots[i].toObject()["blendOverlaps"].toBool();
    static_cast<ViewConfiguration &>(parsed) = common;
    const auto referenced = referencedTemplateSignals(parsed);
    if (referenced.size() != parsed.series.size()) return fail("signal references");
    for (int id : referenced) if (!parsed.series.contains(id)) return fail("signal reference");
    for (const auto &plot : parsed.plots) for (const auto &track : plot.trajectory.entries())
        if (!track.objectId.isEmpty() && !aircraft.contains(track.objectId)) return fail("trajectory.objectId");
    *view = parsed;
    return true;
}
bool readViewTemplate(const QString &path, ViewTemplateDocument *view, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
    if (file.size() > maxBytes) { *error = QStringLiteral("视图模板超过 16 MiB 上限"); return false; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.read(maxBytes + 1), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { *error = QStringLiteral("无效的视图模板 JSON"); return false; }
    return viewTemplateFromJson(document.object(), view, error);
}
bool writeViewTemplate(const QString &path, const ViewTemplateDocument &view, QString *error)
{
    for (const auto &signal : view.series) if (!signal.color.isValid()) {
        *error = QStringLiteral("视图模板包含无效信号颜色"); return false;
    }
    for (const auto &plot : view.plots) for (const auto &track : plot.trajectory.entries()) if (!track.color.isValid()) {
        *error = QStringLiteral("视图模板包含无效航迹颜色"); return false;
    }
    const auto json = viewTemplateToJson(view);
    ViewTemplateDocument validated;
    if (!viewTemplateFromJson(json, &validated, error)) return false;
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Indented);
    if (bytes.size() > maxBytes) { *error = QStringLiteral("视图模板超过 16 MiB 上限"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); return false;
    }
    return true;
}
