#include "appcontroller.h"
#include "items/plotitem.h"


#include <QUuid>
#include <algorithm>
#include <utility>
#include <cmath>
#include <QScopedValueRollback>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString packetLossOutputName(const ObjectRule &rule);
bool configureRuleOutputs(ObjectRule &rule, const QVariantMap &config, int &nextSeries, const ObjectRule *previous, QString &error)
{
    const auto finishMerge=[&] {
        if(rule.operation!="merge") { for(auto &output:rule.outputs) output.inputs.clear(); return; }
        if(rule.outputs.size()==1 && (!config.contains("outputs") || rule.outputs[0].inputs.isEmpty())) rule.outputs[0].inputs=rule.inputs;
        rule.inputs.clear();
        for(const auto &output:rule.outputs) for(const auto &input:output.inputs) if(!rule.inputs.contains(input)) rule.inputs.append(input);
    };
    if(!config.contains("outputs")) {
        if(previous) {
            rule.outputs=previous->outputs;
            if(rule.operation=="bits" && rule.outputs.size()!=rule.bitCount) { error=QStringLiteral("拆位数量变化请使用输出列表编辑"); return false; }
        } else {
            const auto names=config.value("outputNames").toStringList();
            const int count=rule.operation=="bits" ? rule.bitCount : 1;
            for(int k=0;k<count;++k) rule.outputs.append({newId(),names.value(k,rule.operation=="bits" ? rule.name+".bit"+QString::number(rule.startBit+k) : rule.operation=="packetloss" ? packetLossOutputName(rule) : rule.name),nextSeries++});
        }
        finishMerge(); return true;
    }
    const auto specs=config.value("outputs").toList();
    if((rule.operation!="bitfield" && rule.operation!="expression" && rule.operation!="merge") || specs.isEmpty() || specs.size()>32) { error=QStringLiteral("位提取／公式／合并须有 1–32 条输出"); return false; }
    QSet<QString> used;
    rule.outputs.clear();
    for(int k=0;k<specs.size();++k) {
        const auto spec=specs[k].toMap(); ObjectOutput output;
        const auto id=spec.value("id").toString();
        if(!id.isEmpty()) {
            bool found=false;
            if(previous) for(const auto &saved:previous->outputs) if(saved.id==id) { output=saved; found=true; break; }
            if(!found || used.contains(id)) { error=QStringLiteral("输出身份无效或重复"); return false; }
        } else { output.id=newId(); output.series=nextSeries++; }
        used.insert(output.id);
        output.name=spec.value("name").toString().trimmed();
        if(output.name.isEmpty()) output.name=specs.size()==1 ? rule.name : rule.name+QStringLiteral(".输出%1").arg(k+1);
        output.expression=spec.value("expression").toString();
        output.inputs.clear();
        if(rule.operation=="merge") for(const auto &input:spec.value("inputs").toList()) output.inputs.append(input.toString());
        auto integer=[&](const QString &key,int fallback,int &target) {
            bool ok=false; const double value=spec.value(key,fallback).toDouble(&ok);
            if(!ok || !std::isfinite(value) || std::floor(value)!=value || value<0 || value>32) return false;
            target=int(value); return true;
        };
        if(!integer("startBit",0,output.startBit) || !integer("bitCount",1,output.bitCount)) { error=QStringLiteral("位范围须为整数"); return false; }
        output.signedField=spec.value("signedField",false).toBool();
        rule.outputs.append(output);
    }
    finishMerge(); return true;
}
QSet<int> outputRows(const QVector<DataObject> &objects)
{
    QSet<int> rows;
    for (const auto &object : objects) for (const auto &rule : object.rules)
        for (const auto &output : rule.outputs) rows.insert(output.series);
    return rows;
}
bool validName(const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 256 && !name.contains(QChar::Null); }
QString packetLossOutputName(const ObjectRule &rule) {
    return rule.name + (rule.rateMode == "count" ? QStringLiteral(".累计丢包数")
        : rule.rateMode == "window" ? QStringLiteral(".窗口丢包率(%)") : QStringLiteral(".累计丢包率(%)"));
}
bool readCounterConfig(const QVariantMap &config, ObjectRule &rule)
{
    rule.rateMode = config.value("rateMode", rule.rateMode).toString();
    { bool ok = true; rule.rateWindowSeconds = config.value("rateWindowSeconds", rule.rateWindowSeconds).toDouble(&ok); if (!ok) return false; }
    rule.lossMode = config.value("lossMode", rule.lossMode).toString();
    { bool ok = true; rule.counterStep = config.value("counterStep", rule.counterStep).toDouble(&ok); if (!ok) return false; }
    { bool ok = true; rule.expectedStride = config.value("expectedStride", rule.expectedStride).toDouble(&ok); if (!ok) return false; }
    { bool ok = true; rule.counterModulus = config.value("counterModulus", rule.counterModulus).toDouble(&ok); if (!ok) return false; }
    { bool ok = true; rule.counterRate = config.value("counterRate", rule.counterRate).toDouble(&ok); if (!ok) return false; }
    { bool ok = true; rule.counterTolerance = config.value("counterTolerance", rule.counterTolerance).toDouble(&ok); if (!ok) return false; }
    return true;
}
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

QVariantList AppController::objectConfigurations() const
{
    QVariantList result;
    for (const auto &object : m_objects)
        if (!isProcessingConfiguration(object)) result.append(configurationToVariant(object));
    return result;
}

QVariantList AppController::processingConfigurations() const
{
    QVariantList result;
    for (const auto &object : m_objects)
        if (isProcessingConfiguration(object)) result.append(configurationToVariant(object, true));
    return result;
}

QVariantList AppController::processingRuleInputs(const QString &id, const QString &beforeRule) const
{
    const auto selected = std::find_if(m_objects.cbegin(), m_objects.cend(), [&](const DataObject &object) {
        return object.id == id && isProcessingConfiguration(object);
    });
    if (selected == m_objects.cend()) return {};
    if (!beforeRule.isEmpty() && std::none_of(selected->rules.cbegin(), selected->rules.cend(),
        [&](const ObjectRule &rule) { return rule.id == beforeRule; })) return {};
    QVariantList inputs;
    const auto derived = outputRows(m_objects);
    for (int row = 0; row < signalCount(); ++row) if (!derived.contains(row))
        inputs.append(QVariantMap{{"id", "raw:" + QString::number(row)},
            {"name", m_signals->groupAt(row) + " / " + signalName(row)}});
    for (const auto &field : selected->fields) if (field.series < 0)
        inputs.append(QVariantMap{{"id", field.id}, {"name", field.name + QStringLiteral("（来源已移除）")}});
    for (const auto &rule : selected->rules) {
        if (rule.id == beforeRule) break;
        for (const auto &output : rule.outputs)
            inputs.append(QVariantMap{{"id", output.id}, {"name", signalName(output.series)}});
    }
    return inputs;
}

bool AppController::normalizeProcessingInputs(DataObject &definition, ObjectRule &rule) const
{
    QHash<QString,QString> normalized;
    for (auto &input : rule.inputs) {
        const auto original=input;
        if (!input.startsWith("raw:")) continue;
        bool ok = false;
        const int row = input.mid(4).toInt(&ok);
        if (!ok || row < 0 || row >= signalCount() || isObjectOutput(row)) return false;
        QString id;
        for (const auto &source : definition.fields) if (source.series == row) { id = source.id; break; }
        if (id.isEmpty()) {
            id = newId();
            definition.fields.append({id, signalName(row).left(256), QStringLiteral("scalar"), row});
        }
        input = id; normalized.insert(original,id);
    }
    for(auto &output:rule.outputs) for(auto &input:output.inputs) input=normalized.value(input,input);
    return true;
}

QVariantMap AppController::mergeSignals(const QVariantList &groups)
{
    if (m_loading || m_exporting || deriving() || sessionInteractionBlocked() || groups.isEmpty() || groups.size() > 32768)
        return {{"error", QStringLiteral("Merge request is empty, too large, or controller is busy")}};
    auto next = m_objects;
    QVariantList created;
    QVector<PlotSeriesInput> placeholders;
    QVector<QColor> colors;
    QStringList names, outputGroups;
    int paletteIndex = m_nextColorIndex;
    DataObject *target = nullptr;
    for (const auto &value : groups) {
        const auto group = value.toMap();
        const auto name = group.value("name").toString();
        const auto sources = group.value("signals").toList();
        if (!validName(name) || sources.size() < 2 || sources.size() > 256)
            return {{"error", QStringLiteral("Each merge requires a name and 2-256 distinct raw sources")}};
        QSet<int> seen;
        QVector<int> rows;
        for (const auto &source : sources) {
            bool ok = false; const double number = source.toDouble(&ok);
            if (!ok || !std::isfinite(number) || std::floor(number) != number || number < 0 || number >= signalCount())
                return {{"error", QStringLiteral("Invalid merge source")}};
            const int row = int(number);
            if (seen.contains(row) || isObjectOutput(row))
                return {{"error", QStringLiteral("Merge sources must be distinct raw signals")}};
            seen.insert(row); rows.append(row);
        }
        if (!target || target->fields.size() + sources.size() > 256 || target->rules.size() >= 256) {
            const auto count = std::count_if(next.cbegin(), next.cend(), [](const DataObject &definition) {
                return isProcessingConfiguration(definition);
            });
            if (count >= 256) return {{"error", QStringLiteral("Processing configuration limit exceeded")}};
            DataObject object; object.id = newId();
            object.type = "processing";
            object.name = QStringLiteral("同源信号合并 %1").arg(next.size() + 1);
            next.append(object); target = &next.last();
        }
        ObjectRule rule; rule.id = newId(); rule.name = name.trimmed(); rule.operation = "merge";
        for (int row : rows) {
            ObjectField field{newId(), signalName(row).left(256), QStringLiteral("scalar"), row};
            rule.inputs.append(field.id); target->fields.append(field);
        }
        const int outputRow = signalCount() + placeholders.size();
        rule.outputs.append({newId(), rule.name, outputRow});
        target->rules.append(rule);
        PlotSeriesInput input; input.id = outputRow; input.sourceFile = "object:" + target->id;
        input.sourceTable = target->rules.size() - 1; input.sourceColumn = 0;
        input.sourceTableName = target->name + "/" + rule.name;
        input.color = signalPalette().at(paletteIndex); paletteIndex = (paletteIndex + 1) % signalPalette().size();
        placeholders.append(input); colors.append(input.color); names.append(rule.name); outputGroups.append(objectOutputGroup(*target));
        created.append(QVariantMap{{"object", target->id}, {"rule", rule.id}, {"signal", outputRow}, {"name", rule.name}, {"inputs", sources}});
    }
    QString error;
    if (!validateObjects(next, signalCount() + placeholders.size(), &error)) return {{"error", error}};
    // Install the complete validated batch, then evaluate once. No per-field jobs.
    m_objects = std::move(next); m_nextColorIndex = paletteIndex;
    m_seriesStore->appendSeries(placeholders); m_signalColors.append(colors);
    m_signals->appendNames(names, outputGroups, colors);
    markSessionModified(); scheduleObjectEvaluation(); notifyPlotBindingsChanged(); emit currentFileChanged();
    return {{"outputs", created}, {"accepted", true}, {"completed", false}};
}

bool AppController::applyPacketLossGroups(const QString &objectId, const QVector<PacketLossGroup> &groups, const QString &rateMode, double rateWindowSeconds)
{
    auto next = m_objects;
    auto target = std::find_if(next.begin(), next.end(), [&](const DataObject &object) { return object.id == objectId && isProcessingConfiguration(object); });
    if (target == next.end()) { setStatus(QStringLiteral("请选择独立处理配置后重新识别")); return false; }
    if (groups.size() > 256) { setStatus(QStringLiteral("自动识别未提交：超过 256 个采样组，请手动分配到多个处理配置")); return false; }
    QVector<PlotSeriesInput> placeholders; QStringList names, outputGroups; QVector<QColor> colors;
    int paletteIndex = m_nextColorIndex;
    int added = 0;
    for (const auto &group : groups) {
        if (group.sources.isEmpty()) continue;
        const QSet<int> sourceRows(group.sources.begin(), group.sources.end());
        QStringList matchingFields;
        for (const auto &field : target->fields) if (sourceRows.contains(field.series)) matchingFields.append(field.id);
        bool exists = false;
        for (const auto &rule : target->rules)
            if (rule.operation == "packetloss" && rule.lossMode == "time" && rule.rateMode == rateMode
                && (rateMode != "window" || rule.rateWindowSeconds == rateWindowSeconds) && rule.inputs.size() == 1 && matchingFields.contains(rule.inputs.first())) exists = true;
        if (exists) continue; // Preserve identities, edited periods and downstream references.
        QString field;
        if (!matchingFields.isEmpty()) field = matchingFields.first();
        else {
            field = newId();
            target->fields.append({field, group.name, QStringLiteral("scalar"), group.sources.first()});
        }
        ObjectRule rule; rule.id = newId(); rule.name = group.name; rule.operation = "packetloss";
        rule.inputs = {field}; rule.samplePeriod = group.samplePeriod;
        rule.rateMode = rateMode; rule.rateWindowSeconds = rateWindowSeconds;
        for (int k = 0; k < 1; ++k) {
            const QString name = packetLossOutputName(rule);
            const int row = signalCount() + placeholders.size();
            rule.outputs.append({newId(), name, row});
            PlotSeriesInput input; input.id = row; input.sourceFile = "object:" + target->id;
            input.sourceTable = target->rules.size(); input.sourceColumn = k; input.sourceTableName = target->name + "/" + rule.name; input.step = true;
            input.color = signalPalette().at(paletteIndex); paletteIndex = (paletteIndex + 1) % signalPalette().size();
            placeholders.append(input); colors.append(input.color); names.append(name); outputGroups.append(objectOutputGroup(*target));
        }
        target->rules.append(rule); ++added;
    }
    QString error;
    if (!validateObjects(next, signalCount() + placeholders.size(), &error)) {
        setStatus(QStringLiteral("自动识别未提交：%1（每对象最多 256 个字段和规则）").arg(error)); return false;
    }
    if (!added) { setStatus(QStringLiteral("自动识别完成：%1 个采样组均已有检测，已保留当前配置").arg(groups.size())); emit objectsChanged(); return true; }
    m_objects = std::move(next); m_nextColorIndex = paletteIndex;
    m_seriesStore->appendSeries(placeholders); m_signalColors.append(colors); m_signals->appendNames(names, outputGroups, colors);
    markSessionModified(); scheduleObjectEvaluation(); notifyPlotBindingsChanged(); emit currentFileChanged();
    setStatus(QStringLiteral("自动识别完成：新增 %1 个采样组的丢包检测").arg(added));
    return true;
}

double AppController::estimateObjectSamplePeriod(const QString &objectId, const QString &inputId) const
{
    int row = -1;
    if (inputId.startsWith("raw:")) {
        bool ok = false; const int candidate = inputId.mid(4).toInt(&ok);
        if (ok && candidate >= 0 && candidate < signalCount() && !isObjectOutput(candidate)) row = candidate;
    }
    for (const auto &object : m_objects) if (object.id == objectId) {
        for (const auto &field : object.fields) if (field.id == inputId) row = field.series;
        for (const auto &rule : object.rules) for (const auto &output : rule.outputs) if (output.id == inputId) row = output.series;
    }
    const auto snapshot = m_seriesStore->snapshot({row});
    if (snapshot.series.isEmpty()) return 0;
    const auto &source = snapshot.series.first();
    return estimateSamplingPeriod(source->sampleCount(), [&](qsizetype i) { return source->pointAt(i).x(); });
}

QString AppController::objectOutputGroup(const DataObject &object) const
{
    QString name = object.name; name.replace('/', QChar(0x2215));
    return isProcessingConfiguration(object)
        ? QStringLiteral("处理/%1 [%2]").arg(name, object.id.left(8))
        : QStringLiteral("对象/%1 [%2]/派生数据").arg(name, object.id.left(8));
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
    const auto derived = outputRows(m_objects);
    for (int row = 0; row < signalCount(); ++row) if (!derived.contains(row))
        options.append(QVariantMap{{"id", row}, {"name", m_signals->groupAt(row) + "/" + signalName(row)}});
    return options;
}
QVariantList AppController::dataObjects() const
{
    QVariantList result;
    for (const auto &object : m_objects) result.append(configurationToVariant(object));
    return result;
}

QVariantMap AppController::configurationToVariant(const DataObject &object, bool processing) const
{
    QVariantList fields, rules, inputs;
    QHash<QString, QString> inputNames, inputIds;
    for (const auto &field : object.fields) {
        const QVariantMap item{{"id", field.id}, {"name", field.name}, {"role", field.role},
            {"fixed", object.type == "aircraft" && aircraftFieldRoles().contains(field.role)},
            {"series", field.series}, {"source", field.series < 0 ? QStringLiteral("未绑定") : m_signals->groupAt(field.series) + " / " + signalName(field.series)}};
        inputNames.insert(field.id, field.name);
        inputIds.insert(field.id, processing && field.series >= 0 ? "raw:" + QString::number(field.series) : field.id);
        fields.append(item); if (!processing) inputs.append(QVariantMap{{"id", field.id}, {"name", field.name}});
    }
    for (const auto &rule : object.rules) {
        QVariantList outputs; QStringList sources; bool hasSamples = false; int outputPosition=0;
        QStringList ids;
        for (const auto &id : rule.inputs) {
            if (inputNames.contains(id)) sources.append(inputNames.value(id));
            ids.append(inputIds.value(id, id));
        }
        for (const auto &output : rule.outputs) {
            QStringList preview;
            const auto snapshot = m_seriesStore->snapshot({output.series});
            hasSamples |= !snapshot.series.isEmpty() && snapshot.series.first()->sampleCount() > 0;
            if (!snapshot.series.isEmpty()) for (qsizetype i = 0; i < qMin(qsizetype(8), snapshot.series.first()->sampleCount()); ++i) {
                const auto sample = snapshot.series.first()->pointAt(i);
                preview.append(std::isfinite(sample.y()) ? QString::number(sample.y(), 'g', 8) : QStringLiteral("缺失"));
            }
            QVariantList groupInputs;
            if(rule.operation=="merge") for(const auto &input:mergeOutputInputs(rule,output)) groupInputs.append(inputIds.value(input,input));
            outputs.append(QVariantMap{{"id", output.id}, {"name", signalName(output.series)}, {"series", output.series},
                {"checked", plotSignalEnabled(activePlotIndex(), output.series)}, {"preview", preview.join(" / ")}, {"inputs",groupInputs},
                {"startBit",rule.operation=="bits" ? rule.startBit+outputPosition : outputStartBit(rule,output)},{"bitCount",rule.operation=="bits" ? 1 : outputBitCount(rule,output)},{"signedField",rule.operation=="bits" ? false : outputSignedField(rule,output)},{"expression",outputExpression(rule,output)}});
            ++outputPosition;
            inputNames.insert(output.id, signalName(output.series));
            if (!processing) inputs.append(QVariantMap{{"id", output.id}, {"name", signalName(output.series)}});
        }
        rules.append(QVariantMap{{"id", rule.id}, {"name", rule.name}, {"operation", rule.operation},
            {"duplicateSamples", m_objectEvaluationStates.value(rule.id).duplicateSamples}, {"effectiveSamplePeriod", m_objectEvaluationStates.value(rule.id).samplePeriod}, {"inputIds", ids},
            {"sources", sources.join("、")}, {"expression", rule.expression}, {"factor", rule.factor}, {"bias", rule.bias}, {"samplePeriod", rule.samplePeriod}, {"rateMode", rule.rateMode}, {"rateWindowSeconds", rule.rateWindowSeconds}, {"lossMode", rule.lossMode}, {"counterStep", rule.counterStep}, {"expectedStride", rule.expectedStride}, {"counterModulus", rule.counterModulus}, {"counterRate", rule.counterRate}, {"counterTolerance", rule.counterTolerance},
            {"wordBits", rule.wordBits}, {"autoWordBits",rule.autoWordBits},{"byteOrder",rule.byteOrder},{"effectiveWordBits",m_objectEvaluationStates.value(rule.id).effectiveWordBits}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
            {"signedField", rule.signedField}, {"outputs", outputs}, {"error", m_objectErrors.value(rule.id)},
            {"warning", hasSamples && !m_objectErrors.value(rule.id).isEmpty()},
            {"state", deriving() ? "pending" : !hasSamples && !m_objectErrors.value(rule.id).isEmpty() ? "failed" : "succeeded"}});
    }
    return QVariantMap{{"id", object.id}, {"name", object.name}, {"type", object.type}, {"fields", fields}, {"rules", rules}, {"inputs", processing ? QVariantList{} : inputs},
            {"geographic", object.geographic}, {"attitudeMode", object.attitude.mode}, {"radians", object.attitude.radians}, {"order", object.attitude.order},
            {"scalarLast", object.attitude.scalarLast}, {"navigationToBody", object.attitude.navigationToBody}};
}

QString AppController::addDataObject(const QString &type)
{
    QSet<QString> names; for (const auto &object : m_objects) names.insert(object.name);
    const auto pattern = type == "processing" ? QStringLiteral("处理 %1")
        : type == "aircraft" ? QStringLiteral("飞机 %1") : QStringLiteral("对象 %1");
    int number = 1; while (names.contains(pattern.arg(number))) ++number;
    return createDataObject(pattern.arg(number), type);
}
QString AppController::createDataObject(const QString &name, const QString &type)
{
    int sameKind = 0;
    const bool processing = type == "processing";
    for (const auto &definition : m_objects) sameKind += isProcessingConfiguration(definition) == processing;
    if (m_loading || m_exporting || sessionInteractionBlocked() || !validName(name) || sameKind >= 256 || !QStringList{"general", "aircraft", "processing"}.contains(type)) return {};
    DataObject object; object.id = newId(); object.name = name.trimmed(); object.type = type;
    if (type == "aircraft") ensureAircraftFields(object);
    m_objects.append(object);
    markSessionModified(); emit objectsChanged(); return object.id;
}
bool AppController::setDataObjectType(const QString &id, const QString &type)
{
    if (m_loading || m_exporting || sessionInteractionBlocked() || !QStringList{"general", "aircraft", "processing"}.contains(type)) return false;
    for (auto &object : m_objects) if (object.id == id) {
        if (object.type == type) return true;
        if (isProcessingConfiguration(object) != (type == "processing")) return false;
        if (type != "aircraft") for (const auto &state : m_trajectories) for (const auto &track : state.entries())
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
        if (object.type != "aircraft" && role != "scalar" && !existingRole) { setStatus(QStringLiteral("常规对象字段只支持数值")); return {}; }
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
    rule.autoWordBits=config.value("autoWordBits",false).toBool(); rule.byteOrder=config.value("byteOrder","little").toString();
    bool factorOk = true, biasOk = true, periodOk = true;
    rule.factor = config.value("factor", 1).toDouble(&factorOk); rule.bias = config.value("bias", 0).toDouble(&biasOk);
    rule.samplePeriod = config.value("samplePeriod", 0).toDouble(&periodOk);
    if (!factorOk || !biasOk || !periodOk || !readCounterConfig(config, rule)) return {};
    if (rule.operation == "packetloss" && !config.contains("rateMode")) rule.rateMode = "window";
    int nextSeries=signalCount(); QString outputError;
    if(!configureRuleOutputs(rule,config,nextSeries,nullptr,outputError)) { setStatus(outputError); return {}; }
    bool found = false;
    for (auto &object : next) if (object.id == objectId && isProcessingConfiguration(object)) {
        if (!normalizeProcessingInputs(object, rule)) { setStatus(QStringLiteral("请选择有效的原始信号来源")); return {}; }
        object.rules.append(rule); found = true;
    }
    QString error;
    if (!found) { setStatus(QStringLiteral("请选择独立处理配置；对象不再包含派生规则")); return {}; }
    if (!validateObjects(next, nextSeries, &error)) { setStatus(error); return {}; }
    m_objects = next;
    for(const auto &object:m_objects) if(object.id==objectId) appendRuleOutputs(object,rule,signalCount());
    markSessionModified(); scheduleObjectEvaluation(); notifyPlotBindingsChanged(); emit currentFileChanged(); return rule.id;
}
bool AppController::editObjectRule(const QString &objectId, const QString &ruleId, const QVariantMap &config)
{
    if (m_loading || m_exporting || sessionInteractionBlocked()) return false;
    auto next = m_objects; bool found = false; int nextSeries=signalCount(); QSet<int> removedRows;
    for (auto &object : next) if (object.id == objectId && isProcessingConfiguration(object)) for (auto &rule : object.rules) if (rule.id == ruleId) {
        const auto previous = rule;
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
        bool factorOk = true, biasOk = true, periodOk = true;
        rule.factor = config.value("factor", rule.factor).toDouble(&factorOk); rule.bias = config.value("bias", rule.bias).toDouble(&biasOk);
        rule.signedField = config.value("signedField", rule.signedField).toBool();
        rule.autoWordBits=config.value("autoWordBits",rule.autoWordBits).toBool(); rule.byteOrder=config.value("byteOrder",rule.byteOrder).toString();
        rule.samplePeriod = config.value("samplePeriod", rule.samplePeriod).toDouble(&periodOk);
        if (!ok || !factorOk || !biasOk || !periodOk || !readCounterConfig(config, rule)) { setStatus(QStringLiteral("派生参数必须为合法数值")); return false; }
        QString why;
        if(!configureRuleOutputs(rule,config,nextSeries,&previous,why)) { setStatus(why); return false; }
        if (!normalizeProcessingInputs(object, rule)) { setStatus(QStringLiteral("请选择有效的原始信号来源")); return false; }
        QSet<QString> retained;
        for(const auto &output:rule.outputs) retained.insert(output.id);
        for(const auto &output:previous.outputs) if(!retained.contains(output.id)) {
            for(const auto &other:object.rules) if(other.inputs.contains(output.id)) { setStatus(QStringLiteral("输出仍被下游规则引用，请先解除该输入")); return false; }
            removedRows.insert(output.series);
        }
        if (rule.operation == "packetloss" && previous.operation == "packetloss") {
            const int index = 0;
            const auto oldName = previous.outputs[index].name;
            if (oldName == previous.name + ".丢包率(%)" || oldName == previous.name + ".累计丢包数" || oldName == previous.name + ".累计丢包率(%)" || oldName == previous.name + ".窗口丢包率(%)")
                rule.outputs[index].name = packetLossOutputName(rule);
        }
    }
    QString error;
    if (!found || !validateObjects(next, nextSeries, &error)) { setStatus(error); return false; }
    cancelObjectEvaluation();
    const int originalCount=signalCount(); m_objects=next;
    for(const auto &object:m_objects) if(object.id==objectId) for(const auto &rule:object.rules) if(rule.id==ruleId) appendRuleOutputs(object,rule,originalCount);
    if(!removedRows.isEmpty()) removeObjectSeries(removedRows);
    for (const auto &object : m_objects) for (const auto &rule : object.rules) if (rule.id == ruleId)
        for (const auto &output : rule.outputs) m_signals->renameSignal(output.series, output.name);
    markSessionModified(); scheduleObjectEvaluation(); notifyPlotBindingsChanged();
    if(nextSeries!=originalCount) emit currentFileChanged();
    return true;
}

void AppController::appendRuleOutputs(const DataObject &object, const ObjectRule &rule, int existingCount)
{
    QStringList names,groups; QVector<QColor> colors; QVector<PlotSeriesInput> placeholders;
    int ruleIndex=0; for(int i=0;i<object.rules.size();++i) if(object.rules[i].id==rule.id) ruleIndex=i;
    for(const auto &output:rule.outputs) if(output.series>=existingCount) {
        PlotSeriesInput input; input.id=output.series; input.sourceFile="object:"+object.id;
        input.sourceTable=ruleIndex; input.sourceColumn=int(&output-rule.outputs.data()); input.sourceTableName=object.name+"/"+rule.name;
        input.step=rule.operation=="bits" || rule.operation=="bitfield" || rule.operation=="packetloss";
        input.color=signalPalette().at(m_nextColorIndex); m_nextColorIndex=(m_nextColorIndex+1)%signalPalette().size();
        names.append(output.name); groups.append(objectOutputGroup(object)); colors.append(input.color); placeholders.append(input);
        if(rule.operation=="bits" || (rule.operation=="bitfield" && outputBitCount(rule,output)==1 && !outputSignedField(rule,output)))
            m_signalEnums.insert(output.series,{{0,{QStringLiteral("未生效"),QColor("#687887")}},{1,{QStringLiteral("生效"),QColor("#278451")}}});
    }
    if(placeholders.isEmpty()) return;
    m_seriesStore->appendSeries(placeholders); m_signalColors.append(colors); m_signals->appendNames(names,groups,colors);
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
        QVariantList inputs, outputs; QStringList names;
        for (const auto &input : rule.inputs) inputs.append(mapping.value(input));
        for (const auto &output : rule.outputs) {
            names.append(signalName(output.series));
            QVariantList group;
            if(rule.operation=="merge") for(const auto &input:mergeOutputInputs(rule,output)) group.append(mapping.value(input));
            outputs.append(QVariantMap{{"name", signalName(output.series)},
                {"inputs",group}, {"startBit", outputStartBit(rule, output)}, {"bitCount", outputBitCount(rule, output)},
                {"signedField", outputSignedField(rule, output)}, {"expression", outputExpression(rule, output)}});
        }
        QVariantMap config {{"name", rule.name}, {"operation", rule.operation}, {"inputs", inputs},
            {"expression", rule.expression}, {"wordBits", rule.wordBits}, {"autoWordBits",rule.autoWordBits},{"byteOrder",rule.byteOrder}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
            {"signedField", rule.signedField}, {"factor", rule.factor}, {"bias", rule.bias}, {"samplePeriod", rule.samplePeriod}, {"rateMode", rule.rateMode}, {"rateWindowSeconds", rule.rateWindowSeconds}, {"lossMode", rule.lossMode}, {"counterStep", rule.counterStep}, {"expectedStride", rule.expectedStride}, {"counterModulus", rule.counterModulus}, {"counterRate", rule.counterRate}, {"counterTolerance", rule.counterTolerance}, {"outputNames", names}};
        if (rule.operation == "bitfield" || rule.operation == "expression" || rule.operation == "merge") config.insert("outputs", outputs);
        const QString created = addObjectRule(target, config);
        if (created.isEmpty()) {
            removeDataObject(target);
            m_batchObjectChanges = false;
            scheduleObjectEvaluation();
            return {};
        }
        const auto &copy = m_objects.last().rules.last();
        for (int k = 0; k < rule.outputs.size(); ++k) {
            mapping.insert(rule.outputs[k].id, copy.outputs[k].id);
            const auto enums = m_signalEnums.value(rule.outputs[k].series);
            if (!enums.isEmpty()) m_signalEnums.insert(copy.outputs[k].series, enums);
            else m_signalEnums.remove(copy.outputs[k].series);
        }
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
        const auto entry = m_trajectories.value(plotIndex).activeEntry();
        styleTrajectory(plotIndex, object.name, existing >= 0 ? entry.color : QColor("#0072bd"),
                        existing >= 0 ? entry.width : 2, true);
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
