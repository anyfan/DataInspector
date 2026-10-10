#include "sessiondocument.h"
#include "jsonvalidation.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

namespace {
constexpr qint64 maxBytes = 16 * 1024 * 1024;
constexpr int maxSignals = 100000;
using namespace JsonValidation;
}

QJsonObject sessionToJson(const SessionDocument &s)
{
    QJsonArray files, series;
    for (const auto &path : s.files) files.append(QJsonObject{{"path", path}});
    for (const auto &signal : s.series) {
        series.append(QJsonObject{{"file", signal.file}, {"table", signal.table},
            {"column", signal.column}, {"tableName", signal.tableName},
            {"objectId", signal.objectId}, {"outputId", signal.outputId},
            {"originalName", signal.originalName}, {"name", signal.name},
            {"color", signal.color.name(QColor::HexArgb)}, {"width", signal.width},
            {"style", signal.style}, {"timeOffset", signal.timeOffset}});
    }
    auto root = viewConfigurationToJson(s);
    root.insert("format", "DataInspectorSession"); root.insert("version", SessionDocument::version);
    root.insert("files", files); root.insert("signals", series); root.insert("objects", objectsToJson(s.objects));
    root.insert("cursor", QJsonObject{{"mode", s.cursor.mode}, {"x1", s.cursor.x1}, {"x2", s.cursor.x2}});
    return root;
}
bool sessionFromJson(const QJsonObject &root, SessionDocument *session, QString *error)
{
    auto fail = [&](const QString &field) {
        *error = QStringLiteral("会话格式无效或不受支持：%1").arg(field); return false;
    };
    int version = 0;
    if (root.value("format").toString() != QStringLiteral("DataInspectorSession")
        || !integer(root.value("version"), 1, SessionDocument::version, &version)) return fail("format/version");
    SessionDocument parsed;
    const auto cursor = root.value("cursor").toObject();
    if (!integer(cursor.value("mode"), 0, 2, &parsed.cursor.mode)
        || !number(cursor.value("x1"), &parsed.cursor.x1)
        || !number(cursor.value("x2"), &parsed.cursor.x2)) return fail("cursor");
    if (!root.value("files").isArray() || root.value("files").toArray().size() > 10000)
        return fail("files");
    for (const auto &entry : root.value("files").toArray()) {
        QString path;
        if (!text(entry.toObject().value("path"), &path)) return fail("files.path");
        parsed.files.append(path);
    }
    if (!root.value("signals").isArray() || root.value("signals").toArray().size() > maxSignals)
        return fail("signals");
    QSet<QString> identities;
    for (const auto &entry : root.value("signals").toArray()) {
        const auto obj = entry.toObject();
        SessionSignal signal;
        QString color;
        if (version >= 7 && (!text(obj.value("objectId"), &signal.objectId, true)
            || !text(obj.value("outputId"), &signal.outputId, true))) return fail("signals.objectId/outputId");
        const bool derived = !signal.objectId.isEmpty();
        if (derived != !signal.outputId.isEmpty()) return fail("signals.objectId/outputId");
        if (!integer(obj.value("file"), derived ? -1 : 0, derived ? -1 : parsed.files.size() - 1, &signal.file)
            || !integer(obj.value("table"), 0, maxSignals, &signal.table)
            || !integer(obj.value("column"), 0, maxSignals, &signal.column)
            || !text(obj.value("tableName"), &signal.tableName, true)
            || !text(obj.value("originalName"), &signal.originalName, true)
            || !text(obj.value("name"), &signal.name)
            || !text(obj.value("color"), &color)
            || !(signal.color = QColor(color)).isValid()
            || !number(obj.value("width"), &signal.width) || signal.width < 1 || signal.width > 20
            || !integer(obj.value("style"), 1, 5, &signal.style)
            || !number(obj.value("timeOffset"), &signal.timeOffset)) return fail("signals[]");
        const QString key = derived ? QStringLiteral("object:%1:%2").arg(signal.objectId, signal.outputId)
            : QStringLiteral("%1:%2:%3").arg(signal.file).arg(signal.table).arg(signal.column);
        if (identities.contains(key)) return fail("duplicate signal identity");
        identities.insert(key);
        parsed.series.append(signal);
    }
    if (version >= 7) {
        QString why;
        if (!objectsFromJson(root.value("objects"), parsed.series.size(), &parsed.objects, &why, version)) return fail("objects: " + why);
        QSet<int> owned;
        for (const auto &object : parsed.objects) for (const auto &rule : object.rules) for (const auto &output : rule.outputs) {
            const auto &signal = parsed.series[output.series];
            if (signal.objectId != object.id || signal.outputId != output.id) return fail("objects.output ownership");
            owned.insert(output.series);
        }
        for (int i = 0; i < parsed.series.size(); ++i)
            if (!parsed.series[i].objectId.isEmpty() && !owned.contains(i)) return fail("orphan derived signal");
    }
    QVector<QColor> colors;
    for (const auto &signal : parsed.series) colors.append(signal.color);
    ViewConfiguration view;
    if (!viewConfigurationFromJson(root, parsed.series.size(), &view, error, version, colors)) return false;
    static_cast<ViewConfiguration &>(parsed) = view;
    if (version >= 7) {
        if (version == 7) for (const auto &plot : parsed.plots) for (const auto &track : plot.trajectory.entries())
            for (auto &object : parsed.objects) if (object.id == track.objectId) { object.type = "aircraft"; if (!ensureAircraftFields(object)) return fail("aircraft fields"); }
        if (version < 9) {
            QSet<QString> migrated;
            for (const auto &plot : parsed.plots) for (const auto &track : plot.trajectory.entries()) if (!track.objectId.isEmpty() && !migrated.contains(track.objectId)) {
                for (auto &object : parsed.objects) if (object.id == track.objectId) { object.geographic = track.geographic; object.attitude = track.attitude; }
                migrated.insert(track.objectId);
            }
            for (auto &object : parsed.objects) if (object.type == "aircraft" && !ensureAircraftFields(object)) return fail("aircraft fields");
        }
        QSet<QString> objectIds; for (const auto &object : parsed.objects) if (object.type == "aircraft") objectIds.insert(object.id);
        for (const auto &plot : parsed.plots) for (const auto &track : plot.trajectory.entries())
            if (!track.objectId.isEmpty() && !objectIds.contains(track.objectId)) return fail("trajectory.objectId");
    }
    *session = std::move(parsed);
    return true;
}

bool readSessionDocument(const QString &path, SessionDocument *session, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return false; }
    if (file.size() > maxBytes) { *error = QStringLiteral("会话文件超过 16 MiB 上限"); return false; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.read(maxBytes + 1), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("无效 JSON：%1").arg(parse.errorString()); return false;
    }
    return sessionFromJson(document.object(), session, error);
}

bool writeSessionDocument(const QString &path, const SessionDocument &session, QString *error)
{
    for (const auto &signal : session.series) if (!signal.color.isValid()) {
        *error = QStringLiteral("会话包含无效信号颜色"); return false;
    }
    const auto root = sessionToJson(session);
    SessionDocument validated;
    if (!sessionFromJson(root, &validated, error)) return false;
    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > maxBytes) { *error = QStringLiteral("会话文件超过 16 MiB 上限"); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); return false;
    }
    return true;
}
