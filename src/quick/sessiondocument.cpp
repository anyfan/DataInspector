#include "sessiondocument.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

namespace {
constexpr qint64 maxBytes = 16 * 1024 * 1024;
constexpr int maxSignals = 100000;
bool integer(const QJsonValue &value, int minimum, int maximum, int *out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!qIsFinite(number) || number < minimum || number > maximum || qFloor(number) != number)
        return false;
    *out = int(number);
    return true;
}
bool number(const QJsonValue &value, double *out)
{
    if (!value.isDouble() || !qIsFinite(value.toDouble())) return false;
    *out = value.toDouble();
    return true;
}
bool range(const QJsonValue &value, double *minimum, double *maximum)
{
    const auto array = value.toArray();
    return value.isArray() && array.size() == 2
        && number(array[0], minimum) && number(array[1], maximum)
        && *minimum < *maximum && qIsFinite(*maximum - *minimum);
}
bool text(const QJsonValue &value, QString *out, bool allowEmpty = false)
{
    if (!value.isString() || value.toString().size() > 32768) return false;
    *out = value.toString();
    return !out->contains(QChar::Null) && (allowEmpty || !out->trimmed().isEmpty());
}
}

QJsonObject sessionToJson(const SessionDocument &s)
{
    QJsonArray files, series, plots;
    for (const auto &path : s.files) files.append(QJsonObject{{"path", path}});
    for (const auto &signal : s.series) {
        series.append(QJsonObject{{"file", signal.file}, {"table", signal.table},
            {"column", signal.column}, {"tableName", signal.tableName},
            {"originalName", signal.originalName}, {"name", signal.name},
            {"color", signal.color.name(QColor::HexArgb)}, {"width", signal.width},
            {"style", signal.style}, {"timeOffset", signal.timeOffset}});
    }
    for (const auto &plot : s.plots) {
        QJsonArray bindings;
        for (int signal : plot.seriesIds) bindings.append(signal);
        plots.append(QJsonObject{{"signals", bindings},
            {"yRange", QJsonArray{plot.yMinimum, plot.yMaximum}},
            {"normalizeY", plot.normalizeY}, {"lineWidth", plot.lineWidth}});
    }
    return {{"format", "DataInspectorSession"}, {"version", SessionDocument::version},
        {"files", files}, {"signals", series}, {"plots", plots},
        {"layout", QJsonObject{{"rows", s.rows}, {"columns", s.columns},
            {"active", s.active}, {"solo", s.solo}}},
        {"xRange", QJsonArray{s.xMinimum, s.xMaximum}},
        {"cursor", QJsonObject{{"mode", s.cursor.mode}, {"x1", s.cursor.x1}, {"x2", s.cursor.x2}}}};
}

bool sessionFromJson(const QJsonObject &root, SessionDocument *session, QString *error)
{
    auto fail = [&](const QString &field) {
        *error = QStringLiteral("会话格式无效或不受支持：%1").arg(field); return false;
    };
    int version = 0;
    if (root.value("format").toString() != QStringLiteral("DataInspectorSession")
        || !integer(root.value("version"), 1, 1, &version)) return fail("format/version");
    SessionDocument parsed;
    const auto layout = root.value("layout").toObject();
    if (!integer(layout.value("rows"), 1, 8, &parsed.rows)
        || !integer(layout.value("columns"), 1, 8, &parsed.columns)
        || !integer(layout.value("active"), 0, parsed.rows * parsed.columns - 1, &parsed.active)
        || !integer(layout.value("solo"), -1, parsed.rows * parsed.columns - 1, &parsed.solo)
        || (parsed.solo >= 0 && parsed.solo != parsed.active)) return fail("layout");
    if (!range(root.value("xRange"), &parsed.xMinimum, &parsed.xMaximum)) return fail("xRange");
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
        if (!integer(obj.value("file"), 0, parsed.files.size() - 1, &signal.file)
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
        const QString key = QStringLiteral("%1:%2:%3").arg(signal.file).arg(signal.table).arg(signal.column);
        if (identities.contains(key)) return fail("duplicate signal identity");
        identities.insert(key);
        parsed.series.append(signal);
    }
    if (!root.value("plots").isArray()
        || root.value("plots").toArray().size() != parsed.rows * parsed.columns) return fail("plots");
    parsed.plots.clear();
    for (const auto &entry : root.value("plots").toArray()) {
        const auto obj = entry.toObject();
        SessionPlot plot;
        if (!range(obj.value("yRange"), &plot.yMinimum, &plot.yMaximum)
            || !obj.value("normalizeY").isBool()
            || !number(obj.value("lineWidth"), &plot.lineWidth) || plot.lineWidth < 1 || plot.lineWidth > 12
            || !obj.value("signals").isArray()
            || obj.value("signals").toArray().size() > maxSignals) return fail("plots[]");
        plot.normalizeY = obj.value("normalizeY").toBool();
        QSet<int> used;
        for (const auto &binding : obj.value("signals").toArray()) {
            int signal = -1;
            if (!integer(binding, 0, parsed.series.size() - 1, &signal) || used.contains(signal))
                return fail("plots.signals");
            used.insert(signal); plot.seriesIds.append(signal);
        }
        parsed.plots.append(plot);
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
