#include "appcontroller.h"
#include "plotitem.h"


#include <QUuid>
#include <algorithm>
#include <cmath>
#include <QScopedValueRollback>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validName(const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 256 && !name.contains(QChar::Null); }
void resolveObjectMeasurements(const DataObject &object, SessionTrajectoryEntry &track)
{
    QHash<QString, int> fields;
    for (const auto &field : object.fields) fields.insert(field.role, field.series);
    track.name = object.name;
    track.geographic = object.geographic;
    const auto positions = object.geographic ? QStringList{"latitude", "longitude", "height"} : QStringList{"x", "y", "z"};
    for (int i = 0; i < 3; ++i) track.axes[i] = fields.value(positions[i], -1);
    track.attitude = object.attitude;
    track.attitude.sources.fill(-1);
    const auto roles = track.attitude.mode == 1 ? QStringList{"roll", "pitch", "yaw"}
        : track.attitude.mode == 2 ? (track.attitude.scalarLast ? QStringList{"qx", "qy", "qz", "qw"} : QStringList{"qw", "qx", "qy", "qz"})
        : QStringList{};
    for (int i = 0; i < roles.size(); ++i) track.attitude.sources[i] = fields.value(roles[i], -1);
}
}
bool AppController::deriving() const { return bool(m_objectJob); }
QString AppController::objectOutputGroup(const DataObject &object) const
{
    QString name = object.name; name.replace('/', QChar(0x2215));
    return QStringLiteral("对象/%1 [%2]/派生数据").arg(name, object.id.left(8));
}
bool AppController::isObjectOutput(int row) const
{
    for (const auto &object : m_objects) for (const auto &rule : object.rules)
        for (const auto &output : rule.outputs) if (output.series == row) return true;
    return false;
}
QVariantList AppController::objectSourceOptions() const
{
    QVariantList options;
    options.append(QVariantMap{{"id", -1}, {"name", QStringLiteral("未绑定")}});
    for (int row = 0; row < signalCount(); ++row) if (!isObjectOutput(row))
        options.append(QVariantMap{{"id", row}, {"name", m_signals->groupAt(row) + "/" + signalName(row)}});
    return options;
}
QVariantList AppController::dataObjects() const
{
    QVariantList objects;
    for (const auto &object : m_objects) {
        QVariantList fields, rules, inputs;
        for (const auto &field : object.fields) {
            const QVariantMap item{{"id", field.id}, {"name", field.name}, {"role", field.role},
                {"fixed", object.type == "aircraft" && aircraftFieldRoles().contains(field.role)},
                {"series", field.series}, {"source", field.series < 0 ? QStringLiteral("未绑定") : m_signals->groupAt(field.series) + " / " + signalName(field.series)}};
            fields.append(item); inputs.append(QVariantMap{{"id", field.id}, {"name", field.name}});
        }
        for (const auto &rule : object.rules) {
            QVariantList outputs; QStringList sources;
            for (const auto &id : rule.inputs) for (const auto &v : inputs)
                if (v.toMap().value("id").toString() == id) sources.append(v.toMap().value("name").toString());
            for (const auto &output : rule.outputs) {
                QStringList preview;
                const auto snapshot = m_seriesStore->snapshot({output.series});
                if (!snapshot.series.isEmpty()) for (qsizetype i = 0; i < qMin(qsizetype(8), snapshot.series.first()->sampleCount()); ++i) {
                    const auto sample = snapshot.series.first()->pointAt(i);
                    preview.append(std::isfinite(sample.y()) ? QString::number(sample.y(), 'g', 8) : QStringLiteral("缺失"));
                }
                outputs.append(QVariantMap{{"id", output.id}, {"name", signalName(output.series)}, {"series", output.series},
                    {"checked", plotSignalEnabled(activePlotIndex(), output.series)}, {"preview", preview.join(" / ")}});
                inputs.append(QVariantMap{{"id", output.id}, {"name", signalName(output.series)}});
            }
            rules.append(QVariantMap{{"id", rule.id}, {"name", rule.name}, {"operation", rule.operation},
                {"inputIds", rule.inputs},
                {"sources", sources.join("、")}, {"expression", rule.expression}, {"factor", rule.factor}, {"bias", rule.bias},
                {"wordBits", rule.wordBits}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
                {"signedField", rule.signedField}, {"outputs", outputs}, {"error", m_objectErrors.value(rule.id)}});
        }
        objects.append(QVariantMap{{"id", object.id}, {"name", object.name}, {"type", object.type}, {"fields", fields}, {"rules", rules}, {"inputs", inputs},
            {"geographic", object.geographic}, {"attitudeMode", object.attitude.mode}, {"radians", object.attitude.radians}, {"order", object.attitude.order},
            {"scalarLast", object.attitude.scalarLast}, {"navigationToBody", object.attitude.navigationToBody}});
    }
    return objects;
}

QString AppController::addDataObject(const QString &type)
{
    QSet<QString> names; for (const auto &object : m_objects) names.insert(object.name);
    int number = 1; while (names.contains(QStringLiteral("对象 %1").arg(number))) ++number;
    return createDataObject(QStringLiteral("对象 %1").arg(number), type);
}
QString AppController::createDataObject(const QString &name, const QString &type)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !validName(name) || m_objects.size() >= 256 || !QStringList{"general", "aircraft"}.contains(type)) return {};
    DataObject object; object.id = newId(); object.name = name.trimmed(); object.type = type;
    if (type == "aircraft") ensureAircraftFields(object);
    m_objects.append(object);
    markSessionModified(); emit objectsChanged(); return object.id;
}
bool AppController::setDataObjectType(const QString &id, const QString &type)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !QStringList{"general", "aircraft"}.contains(type)) return false;
    for (auto &object : m_objects) if (object.id == id) {
        if (object.type == type) return true;
        if (type == "general") for (const auto &state : m_trajectories) for (const auto &track : state.entries())
            if (track.objectId == id) { setStatus(QStringLiteral("请先解除该飞机对象的航迹绑定，再切换类型")); return false; }
        if (type == "aircraft" && !ensureAircraftFields(object)) { setStatus(QStringLiteral("字段数量已达上限")); return false; }
        object.type = type;
        markSessionModified(); emit objectsChanged(); return true;
    }
    return false;
}
QString AppController::addDataObjectField(const QString &id)
{
    for (const auto &object : m_objects) if (object.id == id) {
        QSet<QString> names; for (const auto &field : object.fields) names.insert(field.name);
        int number = 1; while (names.contains(QStringLiteral("字段 %1").arg(number))) ++number;
        return bindObjectField(id, {}, QStringLiteral("字段 %1").arg(number), "scalar", -1);
    }
    return {};
}
bool AppController::renameDataObjectField(const QString &id, const QString &fieldId, const QString &name)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !validName(name)) return false;
    for (auto &object : m_objects) if (object.id == id) for (auto &field : object.fields) if (field.id == fieldId) {
        field.name = name.trimmed(); markSessionModified(); emit objectsChanged(); return true;
    }
    return false;
}
bool AppController::configureDataObjectAircraft(const QString &id, const QVariantMap &config)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    for (auto &object : m_objects) if (object.id == id && object.type == "aircraft") {
        auto updated = object;
        for (const auto &key : {"geographic", "radians", "scalarLast", "navigationToBody"})
            if (!config.contains(key) || config[key].metaType().id() != QMetaType::Bool) return false;
        auto integer = [&](const QString &key, int max, int *out) {
            bool ok = false; const double value = config.value(key).toDouble(&ok);
            if (!ok || !std::isfinite(value) || value < 0 || value > max || std::floor(value) != value) return false;
            *out = int(value); return true;
        };
        if (!integer("attitudeMode", 2, &updated.attitude.mode) || !integer("order", 1, &updated.attitude.order)) return false;
        updated.geographic = config["geographic"].toBool(); updated.attitude.radians = config["radians"].toBool();
        updated.attitude.scalarLast = config["scalarLast"].toBool(); updated.attitude.navigationToBody = config["navigationToBody"].toBool();
        if (!ensureAircraftFields(updated)) { setStatus(QStringLiteral("字段数量已达上限")); return false; }
        object = updated; syncObjectTrajectories(); markSessionModified(); emit objectsChanged(); return true;
    }
    return false;
}
bool AppController::renameDataObject(const QString &id, const QString &name)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !validName(name)) return false;
    for (auto &object : m_objects) if (object.id == id) {
        object.name = name.trimmed();
        for (const auto &rule : object.rules) for (const auto &output : rule.outputs)
            m_signals->setSignalGroup(output.series, objectOutputGroup(object));
        syncObjectTrajectories(); markSessionModified(); emit objectsChanged(); return true;
    }
    return false;
}
QString AppController::bindObjectField(const QString &objectId, const QString &fieldId,
                                     const QString &name, const QString &role, int series)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !validName(name) || series < -1 || series >= signalCount() || isObjectOutput(series)) return {};
    auto next = m_objects; QString id;
    for (auto &object : next) if (object.id == objectId) {
        bool existingRole = false;
        for (const auto &field : object.fields) if (field.id == fieldId && field.role == role) existingRole = true;
        if (object.type == "general" && role != "scalar" && !existingRole) { setStatus(QStringLiteral("常规对象字段只支持数值")); return {}; }
        for (const auto &field : object.fields) if (field.id == fieldId && object.type == "aircraft" && aircraftFieldRoles().contains(field.role) && role != field.role) {
            setStatus(QStringLiteral("飞机预置字段用途不可更改")); return {};
        }
        for (auto &field : object.fields) if (field.id == fieldId) { field.name = name.trimmed(); field.role = role; field.series = series; id = field.id; }
        if (id.isEmpty() && fieldId.isEmpty()) { id = newId(); object.fields.append({id, name.trimmed(), role, series}); }
    }
    QString error; if (id.isEmpty() || !validateObjects(next, signalCount(), &error)) { setStatus(error); return {}; }
    m_objects = next; markSessionModified(); scheduleObjectEvaluation(); return id;
}
bool AppController::removeObjectField(const QString &objectId, const QString &fieldId)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    for (auto &object : m_objects) if (object.id == objectId) {
        for (const auto &field : object.fields) if (field.id == fieldId && object.type == "aircraft" && aircraftFieldRoles().contains(field.role)) {
            setStatus(QStringLiteral("飞机预置参数不可删除；可以解除信号绑定")); return false;
        }
        for (const auto &rule : object.rules) if (rule.inputs.contains(fieldId)) {
            setStatus(QStringLiteral("字段仍被派生规则引用；可改为未绑定，或先删除依赖规则")); return false;
        }
        for (int i = 0; i < object.fields.size(); ++i) if (object.fields[i].id == fieldId) {
            object.fields.removeAt(i); markSessionModified(); scheduleObjectEvaluation(); return true;
        }
    }
    return false;
}
QString AppController::addObjectRule(const QString &objectId, const QVariantMap &config)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return {};
    auto next = m_objects; ObjectRule rule;
    rule.id = newId(); rule.name = config.value("name").toString().trimmed(); rule.operation = config.value("operation").toString();
    for (const auto &v : config.value("inputs").toList()) rule.inputs.append(v.toString());
    rule.expression = config.value("expression").toString();
    auto getInt = [&](const QString &key, int fallback, int *out) {
        bool ok = true; const double value = config.contains(key) ? config[key].toDouble(&ok) : fallback;
        if (!ok || !std::isfinite(value) || std::floor(value) != value || value < 0 || value > 32) return false;
        *out = int(value); return true;
    };
    if (!getInt("wordBits", 8, &rule.wordBits) || !getInt("startBit", 0, &rule.startBit) || !getInt("bitCount", 1, &rule.bitCount)) return {};
    rule.signedField = config.value("signedField", false).toBool();
    bool factorOk = true, biasOk = true;
    rule.factor = config.value("factor", 1).toDouble(&factorOk); rule.bias = config.value("bias", 0).toDouble(&biasOk);
    if (!factorOk || !biasOk) return {};
    const int count = rule.operation == "bits" ? rule.bitCount : 1;
    const auto names = config.value("outputNames").toStringList();
    for (int k = 0; k < count; ++k) rule.outputs.append({newId(), names.value(k,
        rule.operation == "bits" ? rule.name + ".bit" + QString::number(rule.startBit + k) : rule.name), signalCount() + k});
    bool found = false;
    for (auto &object : next) if (object.id == objectId) { object.rules.append(rule); found = true; }
    QString error;
    if (!found || !validateObjects(next, signalCount() + count, &error)) { setStatus(error); return {}; }
    m_objects = next;
    QStringList outputNames, groups; QVector<QColor> colors; QVector<PlotSeriesInput> placeholders;
    for (const auto &object : m_objects) if (object.id == objectId) for (const auto &output : rule.outputs) {
        PlotSeriesInput input; input.id = output.series; input.sourceFile = "object:" + objectId;
        input.sourceTable = object.rules.size() - 1; input.sourceColumn = placeholders.size();
        input.sourceTableName = object.name + "/" + rule.name; input.step = rule.operation == "bits" || rule.operation == "bitfield";
        input.color = signalPalette().at(m_nextColorIndex); m_nextColorIndex = (m_nextColorIndex + 1) % signalPalette().size();
        outputNames.append(output.name); groups.append(objectOutputGroup(object)); colors.append(input.color); placeholders.append(input);
    }
    m_seriesStore->appendSeries(placeholders); m_signalColors.append(colors); m_signals->appendNames(outputNames, groups, colors);
    markSessionModified(); scheduleObjectEvaluation(); notifyPlotBindingsChanged(); emit currentFileChanged(); return rule.id;
}
bool AppController::editObjectRule(const QString &objectId, const QString &ruleId, const QVariantMap &config)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    auto next = m_objects; bool found = false;
    for (auto &object : next) if (object.id == objectId) for (auto &rule : object.rules) if (rule.id == ruleId) {
        found = true; rule.name = config.value("name").toString().trimmed(); rule.operation = config.value("operation").toString();
        rule.expression = config.value("expression").toString(); rule.inputs.clear();
        for (const auto &v : config.value("inputs").toList()) rule.inputs.append(v.toString());
        bool ok = true;
        auto readInt = [&](const QString &key, int fallback, int *out) {
            bool converted = true; const double value = config.value(key, fallback).toDouble(&converted);
            if (!converted || !std::isfinite(value) || std::floor(value) != value || value < 0 || value > 32) { ok = false; return; }
            *out = int(value);
        };
        readInt("wordBits", rule.wordBits, &rule.wordBits); readInt("startBit", rule.startBit, &rule.startBit); readInt("bitCount", rule.bitCount, &rule.bitCount);
        bool factorOk = true, biasOk = true;
        rule.factor = config.value("factor", rule.factor).toDouble(&factorOk); rule.bias = config.value("bias", rule.bias).toDouble(&biasOk);
        rule.signedField = config.value("signedField", rule.signedField).toBool();
        if (!ok || !factorOk || !biasOk) { setStatus(QStringLiteral("派生参数必须为合法数值")); return false; }
        const int count = rule.operation == "bits" ? rule.bitCount : 1;
        if (count != rule.outputs.size()) { setStatus(QStringLiteral("修改规则保留输出身份和数量；改变输出数量请新建规则")); return false; }
    }
    QString error;
    if (!found || !validateObjects(next, signalCount(), &error)) { setStatus(error); return false; }
    m_objects = next; markSessionModified(); scheduleObjectEvaluation(); return true;
}

void AppController::removeObjectSeries(const QSet<int> &rows)
{
    const auto removed = m_signals->removeRowsById(rows);
    m_seriesStore->removeSeries(rows); remapSignalReferences(removed);
    for (auto it = removed.crbegin(); it != removed.crend(); ++it) m_signalColors.removeAt(*it);
    for (int i = 0; i < m_plots.size(); ++i) refreshPlot(i, false);
    notifyPlotBindingsChanged(); emit currentFileChanged();
}
bool AppController::removeObjectRule(const QString &objectId, const QString &ruleId)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    for (auto &object : m_objects) if (object.id == objectId) for (int i = 0; i < object.rules.size(); ++i) if (object.rules[i].id == ruleId) {
        QSet<QString> outputs; QSet<int> rows;
        for (const auto &output : object.rules[i].outputs) { outputs.insert(output.id); rows.insert(output.series); }
        for (const auto &rule : object.rules) for (const auto &id : rule.inputs) if (outputs.contains(id)) {
            setStatus(QStringLiteral("仍被其他派生规则引用，请先删除下游规则")); return false;
        }
        object.rules.removeAt(i); cancelObjectEvaluation(); removeObjectSeries(rows);
        markSessionModified(); scheduleObjectEvaluation(); return true;
    }
    return false;
}
bool AppController::removeDataObject(const QString &id)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    for (int i = 0; i < m_objects.size(); ++i) if (m_objects[i].id == id) {
        QSet<int> rows; for (const auto &rule : m_objects[i].rules) for (const auto &output : rule.outputs) rows.insert(output.series);
        cancelObjectEvaluation(); m_objects.removeAt(i); removeObjectSeries(rows);
        markSessionModified(); scheduleObjectEvaluation(); return true;
    }
    return false;
}
QString AppController::duplicateDataObject(const QString &id)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return {};
    DataObject source; for (const auto &object : m_objects) if (object.id == id) source = object;
    if (source.id.isEmpty()) return {};
    const QString target = createDataObject(source.name + QStringLiteral(" 副本"), source.type); if (target.isEmpty()) return {};
    QScopedValueRollback<bool> batch(m_batchObjectChanges, true);
    QHash<QString, QString> mapping;
    m_objects.last().fields.clear();
    for (const auto &field : source.fields) {
        auto copy = field; copy.id = newId();
        m_objects.last().fields.append(copy);
        mapping.insert(field.id, copy.id);
    }
    m_objects.last().geographic = source.geographic; m_objects.last().attitude = source.attitude;
    emit objectsChanged();
    for (const auto &rule : source.rules) {
        QVariantList inputs; QStringList names;
        for (const auto &input : rule.inputs) inputs.append(mapping.value(input));
        for (const auto &output : rule.outputs) names.append(signalName(output.series));
        const QString created = addObjectRule(target, {{"name", rule.name}, {"operation", rule.operation}, {"inputs", inputs},
            {"expression", rule.expression}, {"wordBits", rule.wordBits}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
            {"signedField", rule.signedField}, {"factor", rule.factor}, {"bias", rule.bias}, {"outputNames", names}});
        if (created.isEmpty()) {
            removeDataObject(target);
            m_batchObjectChanges = false;
            scheduleObjectEvaluation();
            return {};
        }
        const auto &copy = m_objects.last().rules.last();
        for (int k = 0; k < rule.outputs.size(); ++k) mapping.insert(rule.outputs[k].id, copy.outputs[k].id);
    }
    m_batchObjectChanges = false;
    scheduleObjectEvaluation();
    return target;
}
bool AppController::showObjectTrajectory(const QString &id, int plotIndex)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || plotIndex < 0 || plotIndex >= m_plotRows * m_plotColumns) return false;
    for (const auto &object : m_objects) if (object.id == id) {
        if (object.type != "aircraft") { setStatus(QStringLiteral("仅飞机对象支持航迹显示")); return false; }
        SessionTrajectoryEntry measurement;
        resolveObjectMeasurements(object, measurement);
        const int boundAxes = std::count_if(measurement.axes.cbegin(), measurement.axes.cend(), [](int row) { return row >= 0; });
        if ((object.geographic && (measurement.axes[0] < 0 || measurement.axes[1] < 0)) || boundAxes < 2) {
            setStatus(object.geographic ? QStringLiteral("请绑定飞机的纬度和经度") : QStringLiteral("请至少绑定飞机 XYZ 中的两个参数")); return false;
        }
        const auto current = m_trajectories.value(plotIndex).entries(); int existing = -1;
        for (int i = 0; i < current.size(); ++i) if (current[i].objectId == id) existing = i;
        if (existing >= 0) selectTrajectory(plotIndex, existing);
        else {
            const auto state = m_trajectories.value(plotIndex);
            if (std::any_of(state.activeEntry().axes.cbegin(), state.activeEntry().axes.cend(), [](int row) { return row >= 0; }) && !addTrajectory(plotIndex)) return false;
        }
        if (!configureTrajectory(plotIndex, true, measurement.axes[0], measurement.axes[1], measurement.axes[2], object.geographic)) return false;
        styleTrajectory(plotIndex, object.name, QColor("#0072bd"), 2, true);
        auto &state = m_trajectories[plotIndex];
        for (const auto &field : object.fields) if (aircraftFieldRoles().contains(field.role)) {
            const int row = field.series;
            if (row >= 0 && !state.signalIds.contains(row)) state.signalIds.append(row);
        }
        m_trajectories[plotIndex].activeEntry().objectId = id;
        syncObjectTrajectories(); setActivePlot(plotIndex);
        return true;
    }
    return false;
}
void AppController::syncObjectTrajectories()
{
    for (auto it = m_trajectories.begin(); it != m_trajectories.end(); ++it) {
        auto &state = it.value(); bool changed = false;
        for (auto &track : state.tracks) {
            if (track.objectId.isEmpty()) continue;
            const auto previous = track;
            const DataObject *object = nullptr;
            for (const auto &candidate : m_objects) if (candidate.id == track.objectId) object = &candidate;
            track.axes.fill(-1); track.attitude.sources.fill(-1);
            if (!object) { track.objectId.clear(); track.attitude.mode = 0; }
            else resolveObjectMeasurements(*object, track);
            changed |= track.objectId != previous.objectId || track.axes != previous.axes
                || !(track.attitude == previous.attitude) || track.name != previous.name || track.geographic != previous.geographic;
            for (int row : track.axes) if (row >= 0 && !state.signalIds.contains(row)) state.signalIds.append(row);
            for (int row : track.attitude.sources) if (row >= 0 && !state.signalIds.contains(row)) state.signalIds.append(row);
        }
        if (changed) {
            refreshPlot(it.key(), false);
        }
    }
    notifyPlotBindingsChanged();
}
