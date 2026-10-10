#include "objectdefinition.h"
#include <QRegularExpression>
#include <cmath>
#include <functional>
#include <QJsonObject>
#include <algorithm>

namespace {
// A bounded arithmetic grammar, compiled once per rule. No script execution.
struct Node { QString op; double value = 0; int left = -1, right = -1; };
class Expression {
public:
    QString text, error;
    int pos = 0, variables = 0;
    QVector<Node> nodes;
    int add(QString op, int left = -1, int right = -1, double value = 0) {
        nodes.append({op, value, left, right}); return nodes.size() - 1;
    }
    void spaces() { while (pos < text.size() && text[pos].isSpace()) ++pos; }
    bool take(QChar c) { spaces(); if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
    int parse(int depth = 0, int precedence = 0) {
        if (depth > 32 || nodes.size() > 256) { error = QStringLiteral("表达式过于复杂"); return -1; }
        spaces(); int lhs = -1;
        if (take('-')) lhs = add("neg", parse(depth + 1, 5));
        else if (take('+')) lhs = parse(depth + 1, 5);
        else if (take('!')) lhs = add("not", parse(depth + 1, 5));
        else if (take('(')) {
            lhs = parse(depth + 1);
            if (!take(')')) error = QStringLiteral("缺少右括号");
        } else {
            spaces(); const int begin = pos;
            if (pos < text.size() && (text[pos].isDigit() || text[pos] == '.')) {
                static const QRegularExpression numeric(QStringLiteral("\\G(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?"));
                const auto match = numeric.match(text, pos);
                bool ok = false; double value = match.captured().toDouble(&ok);
                if (!ok || !std::isfinite(value)) { error = QStringLiteral("无效数值"); return -1; }
                pos += match.capturedLength(); lhs = add("number", -1, -1, value);
            } else {
                while (pos < text.size() && text[pos].isLetter()) ++pos;
                const QString name = text.mid(begin, pos - begin);
                if ((name == "x" || name == "y" || name == "z") && QStringLiteral("xyz").indexOf(name) < variables)
                    lhs = add(name);
                else if (name == "abs" || name == "sqrt" || name == "min" || name == "max") {
                    if (!take('(')) { error = QStringLiteral("函数缺少括号"); return -1; }
                    const int a = parse(depth + 1); int b = -1;
                    if (name == "min" || name == "max") {
                        if (!take(',')) { error = QStringLiteral("min/max 需要两个参数"); return -1; }
                        b = parse(depth + 1);
                    }
                    if (!take(')')) error = QStringLiteral("函数缺少右括号");
                    lhs = add(name, a, b);
                } else { error = QStringLiteral("仅支持已绑定的 x/y/z 与 abs/sqrt/min/max"); return -1; }
            }
        }
        while (error.isEmpty()) {
            spaces(); if (pos >= text.size()) break;
            QString op = text.mid(pos, 2);
            if (!QStringList{"&&", "||", "==", "!=", ">=", "<="}.contains(op)) op = text.mid(pos, 1);
            const int p = op == "||" ? 1 : op == "&&" ? 2
                : QStringList{"==", "!=", ">=", "<=", ">", "<"}.contains(op) ? 3
                : (op == "+" || op == "-") ? 4 : (op == "*" || op == "/") ? 5 : 0;
            if (!p || p <= precedence) break;
            pos += op.size(); const int rhs = parse(depth + 1, p); lhs = add(op, lhs, rhs);
        }
        return lhs;
    }
    int compile(const QString &source, int count) {
        text = source; variables = count;
        if (text.isEmpty() || text.size() > 1024) { error = QStringLiteral("表达式长度应为 1–1024"); return -1; }
        const int root = parse(); spaces();
        if (pos != text.size() && error.isEmpty()) error = QStringLiteral("表达式包含不支持的字符");
        return root;
    }
    double eval(int index, const double *values) const {
        if (index < 0) return qQNaN();
        const auto &n = nodes[index];
        if (n.op == "number") return n.value;
        if (n.op == "x" || n.op == "y" || n.op == "z") return values[QStringLiteral("xyz").indexOf(n.op)];
        const double a = eval(n.left, values);
        if (!std::isfinite(a)) return qQNaN();
        if (n.op == "neg") return -a;
        if (n.op == "not") return a == 0;
        if (n.op == "abs") return std::abs(a);
        if (n.op == "sqrt") return std::sqrt(a);
        const double b = eval(n.right, values);
        if (!std::isfinite(b)) return qQNaN();
        if (n.op == "+") return a + b;
        if (n.op == "-") return a - b;
        if (n.op == "*") return a * b;
        if (n.op == "/") return b == 0 ? qQNaN() : a / b;
        if (n.op == "min") return qMin(a, b);
        if (n.op == "==") return a == b;
        if (n.op == "!=") return a != b;
        if (n.op == ">=") return a >= b;
        if (n.op == "<=") return a <= b;
        if (n.op == ">") return a > b;
        if (n.op == "<") return a < b;
        if (n.op == "&&") return a != 0 && b != 0;
        if (n.op == "||") return a != 0 || b != 0;
        return qMax(a, b);
    }
};
}

QString validateObjectExpression(const QString &expression, int inputCount)
{
    Expression parser; parser.compile(expression, inputCount); return parser.error;
}

QStringList aircraftFieldRoles()
{
    return {"latitude", "longitude", "height", "roll", "pitch", "yaw", "x", "y", "z", "qw", "qx", "qy", "qz"};
}
QStringList activeAircraftFieldRoles(const DataObject &object)
{
    QStringList roles = object.geographic ? QStringList{"latitude", "longitude", "height"} : QStringList{"x", "y", "z"};
    if (object.attitude.mode == 1) roles.append({"roll", "pitch", "yaw"});
    if (object.attitude.mode == 2) roles.append({"qw", "qx", "qy", "qz"});
    return roles;
}
bool ensureAircraftFields(DataObject &object)
{
    const auto roles = aircraftFieldRoles();
    const auto active = activeAircraftFieldRoles(object);
    const QStringList names{QStringLiteral("纬度"), QStringLiteral("经度"), QStringLiteral("高度"), QStringLiteral("滚转"), QStringLiteral("俯仰"), QStringLiteral("航向"), "X", "Y", "Z", "qW", "qX", "qY", "qZ"};
    QSet<QString> present; for (const auto &field : object.fields) present.insert(field.role);
    int missing = 0; for (const auto &role : active) if (!present.contains(role)) ++missing;
    if (object.fields.size() + missing > 256) return false;
    for (int i = 0; i < roles.size(); ++i) if (active.contains(roles[i]) && !present.contains(roles[i]))
        object.fields.append({object.id.left(80) + "-preset-" + roles[i], names[i], roles[i], -1});
    std::stable_sort(object.fields.begin(), object.fields.end(), [&](const ObjectField &a, const ObjectField &b) {
        const int left = roles.indexOf(a.role), right = roles.indexOf(b.role);
        return (left < 0 ? roles.size() : left) < (right < 0 ? roles.size() : right);
    });
    return true;
}
bool validateObjects(const QVector<DataObject> &objects, int signalCount, QString *error)
{
    auto fail = [&](const QString &why) { *error = why; return false; };
    if (objects.size() > 256) return fail(QStringLiteral("对象最多 256 个"));
    QSet<QString> identities; QSet<int> outputSeries;
    auto identity = [&](const QString &id) {
        if (id.isEmpty() || id.size() > 128 || identities.contains(id)) return false;
        identities.insert(id); return true;
    };
    auto nameValid = [](const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 256 && !name.contains(QChar::Null); };
    for (const auto &object : objects) {
        if (!identity(object.id) || !nameValid(object.name) || !QStringList{"general", "aircraft"}.contains(object.type) || object.fields.size() > 256 || object.rules.size() > 256)
            return fail(QStringLiteral("对象身份、名称或数量无效"));
        QSet<QString> available, spatialRoles;
        for (const auto &field : object.fields) {
            if (!identity(field.id) || !nameValid(field.name) || field.series < -1 || field.series >= signalCount
                || (field.role != "scalar" && !aircraftFieldRoles().contains(field.role)))
                return fail(QStringLiteral("对象字段无效"));
            if (aircraftFieldRoles().contains(field.role)) {
                if (spatialRoles.contains(field.role)) return fail(QStringLiteral("同一对象的位置/姿态用途不能重复"));
                spatialRoles.insert(field.role);
            }
            available.insert(field.id);
        }
        if (object.type == "aircraft") {
            for (const auto &role : activeAircraftFieldRoles(object)) if (!spatialRoles.contains(role)) return fail(QStringLiteral("飞机预置字段缺失"));
            if (object.attitude.mode < 0 || object.attitude.mode > 2 || object.attitude.order < 0 || object.attitude.order > 1)
                return fail(QStringLiteral("飞机姿态配置无效"));
        }
        for (const auto &rule : object.rules) {
            if (!identity(rule.id) || !nameValid(rule.name) || rule.inputs.isEmpty() || rule.inputs.size() > 3
                || !std::isfinite(rule.factor) || !std::isfinite(rule.bias)) return fail(QStringLiteral("派生规则无效"));
            for (const auto &input : rule.inputs) if (!available.contains(input))
                return fail(QStringLiteral("派生只能引用本对象字段或此前派生结果，禁止循环依赖"));
            int count = 1;
            if (rule.operation == "bits" || rule.operation == "bitfield") {
                if ((rule.wordBits != 8 && rule.wordBits != 16 && rule.wordBits != 32) || rule.startBit < 0
                    || rule.bitCount < 1 || rule.startBit + rule.bitCount > rule.wordBits || rule.inputs.size() != 1)
                    return fail(QStringLiteral("位宽或位段无效"));
                if (rule.operation == "bits") count = rule.bitCount;
            } else if (rule.operation == "scale") {
                if (rule.inputs.size() != 1) return fail(QStringLiteral("换算需要一个输入"));
            } else if (rule.operation == "magnitude") {
                if (rule.inputs.size() != 3) return fail(QStringLiteral("三轴模长需要三个输入"));
            } else if (rule.operation == "expression") {
                const QString why = validateObjectExpression(rule.expression, rule.inputs.size());
                if (!why.isEmpty()) return fail(why);
            } else return fail(QStringLiteral("不支持的派生算子"));
            if (rule.outputs.size() != count) return fail(QStringLiteral("派生输出数量无效"));
            for (const auto &output : rule.outputs) {
                if (!identity(output.id) || !nameValid(output.name) || output.series < 0 || output.series >= signalCount
                    || outputSeries.contains(output.series)) return fail(QStringLiteral("派生输出身份无效"));
                outputSeries.insert(output.series); available.insert(output.id);
            }
        }
    }
    for (const auto &object : objects) for (const auto &field : object.fields)
        if (outputSeries.contains(field.series)) return fail(QStringLiteral("字段必须绑定原始来源，派生结果通过规则引用"));
    return true;
}

namespace {
QJsonObject calculationKey(const ObjectRule &rule)
{
    QJsonArray outputs;
    for (const auto &output : rule.outputs) outputs.append(QJsonObject{{"id", output.id}, {"series", output.series}});
    return {{"operation", rule.operation}, {"expression", rule.expression},
        {"inputs", QJsonArray::fromStringList(rule.inputs)}, {"outputs", outputs},
        {"wordBits", rule.wordBits}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
        {"signedField", rule.signedField}, {"factor", rule.factor}, {"bias", rule.bias}};
}
bool sameSamples(const PlotSeriesDataPtr &a, const PlotSeriesDataPtr &b)
{
    if (!a || !b) return a == b;
    return a->id == b->id && a->timeOffset == b->timeOffset
        && a->time.constData() == b->time.constData() && a->values.constData() == b->values.constData()
        && a->points.constData() == b->points.constData();
}
}
QSet<QString> invalidatedObjectRules(const QVector<DataObject> &objects,
    const PlotSeriesSnapshot &snapshot, const QHash<QString, ObjectRuleState> &states)
{
    QHash<int, PlotSeriesDataPtr> sources;
    for (const auto &data : snapshot.series) sources.insert(data->id, data);
    QSet<QString> dirty;
    for (const auto &object : objects) {
        QHash<QString, PlotSeriesDataPtr> bindings;
        QSet<QString> changedInputs;
        for (const auto &field : object.fields) bindings.insert(field.id, sources.value(field.series));
        for (const auto &rule : object.rules) {
            const auto previous = states.constFind(rule.id);
            bool changed = previous == states.cend() || previous->definition != calculationKey(rule)
                || previous->inputs.size() != rule.inputs.size();
            for (int i = 0; !changed && i < rule.inputs.size(); ++i)
                changed = changedInputs.contains(rule.inputs[i]) || !sameSamples(previous->inputs[i], bindings.value(rule.inputs[i]));
            for (const auto &output : rule.outputs) if (!sources.contains(output.series)) changed = true;
            if (changed) dirty.insert(rule.id);
            for (const auto &output : rule.outputs) {
                bindings.insert(output.id, sources.value(output.series));
                if (changed) changedInputs.insert(output.id);
            }
        }
    }
    return dirty;
}

ObjectEvaluation evaluateObjects(const QVector<DataObject> &objects, const PlotSeriesSnapshot &snapshot,
                                 const std::atomic_bool &cancelled, qsizetype retainedValues,
                                 const QSet<QString> *rulesToEvaluate)
{
    ObjectEvaluation result; QHash<int, PlotSeriesDataPtr> sources;
    for (const auto &s : snapshot.series) sources.insert(s->id, s);
    for (const auto &object : objects) {
        QHash<QString, PlotSeriesDataPtr> bindings;
        for (const auto &field : object.fields) bindings.insert(field.id, sources.value(field.series));
        for (int ruleIndex = 0; ruleIndex < object.rules.size(); ++ruleIndex) {
            if (cancelled.load()) return {};
            const auto &rule = object.rules[ruleIndex]; QVector<PlotSeriesDataPtr> inputs;
            if (rulesToEvaluate && !rulesToEvaluate->contains(rule.id)) {
                for (const auto &output : rule.outputs) bindings.insert(output.id, sources.value(output.series));
                continue;
            }
            QString error; qsizetype count = 0;
            for (const auto &id : rule.inputs) {
                const auto source = bindings.value(id);
                if (!source || source->sampleCount() == 0) error = QStringLiteral("缺少有效来源");
                if (inputs.isEmpty() && source) count = source->sampleCount();
                if (source && source->sampleCount() != count) error = QStringLiteral("输入样本数不同，要求相同时间基");
                inputs.append(source);
            }
            if (count > (512 * 1024 * 1024 - retainedValues) / (8 * qMax(1, int(rule.outputs.size())))) {
                error = QStringLiteral("派生结果超过 512 MiB 值缓存上限，请减少输出或数据规模");
                result.budgetLimitedRules.insert(rule.id);
            }
            if (error.isEmpty()) retainedValues += count * 8 * rule.outputs.size();
            Expression expression; const int root = rule.operation == "expression" ? expression.compile(rule.expression, inputs.size()) : -1;
            QVector<std::shared_ptr<PlotSeriesData>> outputs;
            for (int k = 0; k < rule.outputs.size(); ++k) {
                auto output = std::make_shared<PlotSeriesData>(); output->id = rule.outputs[k].series;
                output->sourceFile = "object:" + object.id; output->sourceTable = ruleIndex; output->sourceColumn = k;
                output->sourceTableName = object.name + "/" + rule.name;
                output->step = rule.operation == "bits" || rule.operation == "bitfield";
                if (rule.operation == "expression" && root >= 0 && expression.error.isEmpty())
                    output->step = QStringList{"not", "&&", "||", "==", "!=", ">=", "<=", ">", "<"}.contains(expression.nodes[root].op);
                if (error.isEmpty()) {
                    const auto &source = inputs.first();
                    output->timeOffset = source->timeOffset; output->monotonicTime = source->monotonicTime;
                    if (source->points.isEmpty()) output->time = source->time;
                    else { output->time.reserve(count); for (qsizetype i = 0; i < count; ++i) output->time.append(source->pointAt(i).x() - source->timeOffset); }
                    output->values.resize(count, qQNaN());
                }
                outputs.append(output);
            }
            qsizetype invalid = 0;
            if (error.isEmpty()) for (qsizetype i = 0; i < count; ++i) {
                if ((i & 4095) == 0 && cancelled.load()) return {};
                double values[3] = {}; bool valid = true;
                const double time = inputs.first()->pointAt(i).x();
                for (int j = 0; j < inputs.size(); ++j) {
                    const auto point = inputs[j]->pointAt(i);
                    if (j > 0 && point.x() != time && !(std::isnan(point.x()) && std::isnan(time))) {
                        error = QStringLiteral("输入时间基不一致（包含偏移），不进行插值"); break;
                    }
                    values[j] = point.y(); valid = valid && std::isfinite(values[j]) && std::isfinite(time);
                }
                if (!error.isEmpty()) break;
                if (rule.operation == "bits" || rule.operation == "bitfield") {
                    const double limit = std::ldexp(1.0, rule.wordBits);
                    valid = valid && values[0] >= 0 && values[0] < limit && std::floor(values[0]) == values[0];
                    if (!valid) { ++invalid; continue; }
                    const quint64 word = quint64(values[0]);
                    if (rule.operation == "bits") for (int k = 0; k < outputs.size(); ++k) outputs[k]->values[i] = double((word >> (rule.startBit + k)) & 1);
                    else {
                        const quint64 mask = (quint64(1) << rule.bitCount) - 1;
                        const quint64 field = (word >> rule.startBit) & mask;
                        const qint64 value = rule.signedField && (field & (quint64(1) << (rule.bitCount - 1)))
                            ? qint64(field) - qint64(quint64(1) << rule.bitCount) : qint64(field);
                        outputs.first()->values[i] = double(value);
                    }
                } else {
                    if (!valid) { ++invalid; continue; }
                    const double value = rule.operation == "scale" ? values[0] * rule.factor + rule.bias
                        : rule.operation == "magnitude" ? std::hypot(values[0], values[1], values[2]) : expression.eval(root, values);
                    outputs.first()->values[i] = std::isfinite(value) ? value : qQNaN();
                    if (!std::isfinite(value)) ++invalid;
                }
            }
            if (invalid && error.isEmpty()) result.errors.insert(rule.id, QStringLiteral("%1 个无效样本已留空").arg(invalid));
            if (!error.isEmpty()) result.errors.insert(rule.id, error);
            if (!result.budgetLimitedRules.contains(rule.id)) result.states.insert(rule.id, {calculationKey(rule), inputs});
            for (int k = 0; k < outputs.size(); ++k) {
                if (!error.isEmpty()) { outputs[k]->time.clear(); outputs[k]->values.clear(); }
                outputs[k]->rangeIndex = PlotRangeIndex::build(*outputs[k], [&] { return cancelled.load(); });
                if (cancelled.load()) return {};
                bindings.insert(rule.outputs[k].id, outputs[k]); result.series.append(outputs[k]);
            }
        }
    }
    return result;
}

QJsonArray objectsToJson(const QVector<DataObject> &objects)
{
    QJsonArray array;
    for (const auto &object : objects) {
        QJsonArray fields, rules;
        for (const auto &field : object.fields) fields.append(QJsonObject{{"id", field.id}, {"name", field.name}, {"role", field.role}, {"series", field.series}});
        for (const auto &rule : object.rules) {
            QJsonArray inputs, outputs;
            for (const auto &input : rule.inputs) inputs.append(input);
            for (const auto &output : rule.outputs) outputs.append(QJsonObject{{"id", output.id}, {"name", output.name}, {"series", output.series}});
            rules.append(QJsonObject{{"id", rule.id}, {"name", rule.name}, {"operation", rule.operation}, {"expression", rule.expression},
                {"inputs", inputs}, {"outputs", outputs}, {"wordBits", rule.wordBits}, {"startBit", rule.startBit}, {"bitCount", rule.bitCount},
                {"signedField", rule.signedField}, {"factor", rule.factor}, {"bias", rule.bias}});
        }
        array.append(QJsonObject{{"id", object.id}, {"name", object.name}, {"type", object.type}, {"fields", fields}, {"rules", rules},
            {"aircraft", QJsonObject{{"geographic", object.geographic}, {"attitudeMode", object.attitude.mode}, {"radians", object.attitude.radians},
                {"order", object.attitude.order}, {"scalarLast", object.attitude.scalarLast}, {"navigationToBody", object.attitude.navigationToBody}}}});
    }
    return array;
}
bool objectsFromJson(const QJsonValue &value, int signalCount, QVector<DataObject> *objects, QString *error, int version)
{
    auto fail = [&] { *error = QStringLiteral("对象定义格式无效"); return false; };
    auto integer = [](const QJsonValue &v, int *out) {
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || std::floor(v.toDouble()) != v.toDouble()
            || v.toDouble() < -1 || v.toDouble() > 100000) return false;
        *out = int(v.toDouble()); return true;
    };
    auto string = [](const QJsonValue &v, QString *out) {
        if (!v.isString() || v.toString().size() > 1024 || v.toString().contains(QChar::Null)) return false;
        *out = v.toString(); return true;
    };
    if (!value.isArray() || value.toArray().size() > 256) return fail();
    QVector<DataObject> parsed;
    for (const auto &v : value.toArray()) {
        if (!v.isObject()) return fail();
        const auto obj = v.toObject(); DataObject object;
        if (version >= 8 || obj.contains("type")) {
            if (!string(obj["type"], &object.type)) return fail();
        }
        if (!string(obj["id"], &object.id) || !string(obj["name"], &object.name)
            || !obj["fields"].isArray() || obj["fields"].toArray().size() > 256
            || !obj["rules"].isArray() || obj["rules"].toArray().size() > 256) return fail();
        for (const auto &fv : obj["fields"].toArray()) {
            if (!fv.isObject()) return fail();
            const auto f = fv.toObject(); ObjectField field;
            if (!string(f["id"], &field.id) || !string(f["name"], &field.name) || !string(f["role"], &field.role) || !integer(f["series"], &field.series)) return fail();
            object.fields.append(field);
        }
        if (version < 8 && !obj.contains("type"))
            for (const auto &field : object.fields)
                if (field.role == "latitude" || field.role == "longitude") object.type = "aircraft";
        if (version >= 9) {
            if (!obj["aircraft"].isObject()) return fail();
            const auto settings = obj["aircraft"].toObject();
            for (const auto &key : {"geographic", "radians", "scalarLast", "navigationToBody"}) if (!settings[key].isBool()) return fail();
            if (!integer(settings["attitudeMode"], &object.attitude.mode) || object.attitude.mode < 0 || object.attitude.mode > 2
                || !integer(settings["order"], &object.attitude.order) || object.attitude.order < 0 || object.attitude.order > 1) return fail();
            object.geographic = settings["geographic"].toBool(); object.attitude.radians = settings["radians"].toBool();
            object.attitude.scalarLast = settings["scalarLast"].toBool(); object.attitude.navigationToBody = settings["navigationToBody"].toBool();
        } else {
            object.attitude.mode = 1; // Preserve the pre-v9 aircraft default during migration.
            for (auto &field : object.fields) if (object.type == "general" || field.role == "status") field.role = "scalar";
            if (object.type == "aircraft" && !ensureAircraftFields(object)) return fail();
        }
        if (version < 10) {
            for (auto &field : object.fields) if (field.role == "scalar")
                for (const auto &role : aircraftFieldRoles()) if (field.id == object.id.left(80) + "-preset-" + role) field.role = role;
            if (object.type == "aircraft" && !ensureAircraftFields(object)) return fail();
        }
        for (const auto &rv : obj["rules"].toArray()) {
            if (!rv.isObject()) return fail();
            const auto r = rv.toObject(); ObjectRule rule;
            if (!string(r["id"], &rule.id) || !string(r["name"], &rule.name) || !string(r["operation"], &rule.operation)
                || !string(r["expression"], &rule.expression) || !r["inputs"].isArray() || r["inputs"].toArray().size() > 3
                || !r["outputs"].isArray() || r["outputs"].toArray().size() > 32
                || !integer(r["wordBits"], &rule.wordBits) || !integer(r["startBit"], &rule.startBit) || !integer(r["bitCount"], &rule.bitCount)
                || !r["signedField"].isBool() || !r["factor"].isDouble() || !r["bias"].isDouble()) return fail();
            rule.signedField = r["signedField"].toBool(); rule.factor = r["factor"].toDouble(); rule.bias = r["bias"].toDouble();
            for (const auto &input : r["inputs"].toArray()) { QString id; if (!string(input, &id)) return fail(); rule.inputs.append(id); }
            for (const auto &ov : r["outputs"].toArray()) {
                if (!ov.isObject()) return fail();
                const auto o = ov.toObject(); ObjectOutput output;
                if (!string(o["id"], &output.id) || !string(o["name"], &output.name) || !integer(o["series"], &output.series)) return fail();
                rule.outputs.append(output);
            }
            object.rules.append(rule);
        }
        parsed.append(object);
    }
    if (!validateObjects(parsed, signalCount, error)) return false;
    *objects = parsed; return true;
}
