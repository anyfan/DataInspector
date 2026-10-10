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
        if (!keys(plot, {"signals", "yRange", "normalizeY", "lineWidth", "trajectory"})) return false;
        const auto trajectory = plot.value("trajectory").toObject();
        if (!keys(trajectory, {"enabled", "geographic", "tracks", "active", "signals", "rotation", "viewScale", "panDepth", "axes", "camera"})) return false;
        for (const auto &entry : trajectory.value("tracks").toArray()) {
            const auto track = entry.toObject();
            if (!keys(track, {"id", "objectId", "name", "visible", "geographic", "color", "width", "axes", "attitude"})
                || !keys(track.value("attitude").toObject(), {"mode", "sources", "radians", "order", "scalarLast", "navigationToBody"})) return false;
        }
    }
    return true;
}
}
ViewTemplateDocument viewTemplateFromSession(const SessionDocument &session)
{
    ViewTemplateDocument view;
    static_cast<ViewConfiguration &>(view) = session;
    for (auto &plot : view.plots) for (auto &track : plot.trajectory.tracks) track.objectId.clear();
    for (int id : referencedViewSignals(view)) if (id >= 0 && id < session.series.size()) {
        const auto &signal = session.series[id];
        view.series.insert(id, {signal.objectId.isEmpty() ? QFileInfo(session.files.value(signal.file)).fileName() : QStringLiteral("对象"),
            signal.tableName, signal.originalName, signal.name, signal.table, signal.style, signal.color, signal.width});
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
            {"color", s.color.name(QColor::HexArgb)}, {"width", s.width}, {"style", s.style}});
    }
    return {{"format", "DataInspectorViewTemplate"}, {"version", ViewTemplateDocument::version},
        {"signals", signalRows}, {"view", viewConfigurationToJson(view)}};
}
bool viewTemplateFromJson(const QJsonObject &root, ViewTemplateDocument *view, QString *error)
{
    auto fail = [&](const QString &field) { *error = QStringLiteral("视图模板格式无效：%1").arg(field); return false; };
    if (root.value("format") != QJsonValue("DataInspectorViewTemplate")) return fail("format");
    if (root.value("version") == QJsonValue(1)) {
        if (!keys(root, {"format", "version", "view"}) || !root.value("view").isObject()) return fail("v1");
        SessionDocument legacy;
        if (!sessionFromJson(root.value("view").toObject(), &legacy, error)) return false;
        *view = viewTemplateFromSession(legacy);
        return true;
    }
    if (root.value("version") != QJsonValue(ViewTemplateDocument::version)
        || !keys(root, {"format", "version", "signals", "view"})
        || !root.value("signals").isArray() || root.value("signals").toArray().size() > 100000
        || !root.value("view").isObject()) return fail("version/signals/view");
    ViewTemplateDocument parsed;
    for (const auto &value : root.value("signals").toArray()) {
        if (!value.isObject()) return fail("signals[]");
        const auto obj = value.toObject(); int id = -1; QString color;
        ViewTemplateSignal signal;
        if (!keys(obj, {"id", "source", "table", "tableName", "originalName", "name", "color", "width", "style"})
            || !integer(obj.value("id"), 0, 99999, &id) || parsed.series.contains(id)
            || !integer(obj.value("table"), 0, 100000, &signal.table)
            || !text(obj.value("source"), &signal.sourceName, true)
            || !text(obj.value("tableName"), &signal.tableName, true)
            || !text(obj.value("originalName"), &signal.originalName, true)
            || !text(obj.value("name"), &signal.name)
            || !text(obj.value("color"), &color) || !(signal.color = QColor(color)).isValid()
            || !number(obj.value("width"), &signal.width) || signal.width < 1 || signal.width > 20
            || !integer(obj.value("style"), 1, 5, &signal.style)) return fail("signals[]");
        parsed.series.insert(id, signal);
    }
    const auto configuration = root.value("view").toObject();
    if (!viewKeys(configuration)) return fail("view");
    ViewConfiguration common;
    const int signalCount = parsed.series.isEmpty() ? 0 : parsed.series.lastKey() + 1;
    if (!viewConfigurationFromJson(configuration, signalCount, &common, error)) return false;
    static_cast<ViewConfiguration &>(parsed) = common;
    const auto referenced = referencedViewSignals(parsed);
    if (referenced.size() != parsed.series.size()) return fail("signal references");
    for (int id : referenced) if (!parsed.series.contains(id)) return fail("signal reference");
    for (const auto &plot : parsed.plots) for (const auto &track : plot.trajectory.entries())
        if (!track.objectId.isEmpty()) return fail("trajectory.objectId");
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
