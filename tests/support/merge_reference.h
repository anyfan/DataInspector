#pragma once
#include "models/objectdefinition.h"
#include "render/plotrangeindex.h"
#include <algorithm>
#include <cmath>

// Independent point-sort oracle for merge tests and benchmarks only.
// Production targets never include this header or expose a reference-mode switch.
inline ObjectEvaluation evaluateMergeReference(const QVector<DataObject> &objects,
    const PlotSeriesSnapshot &snapshot, const std::atomic_bool &cancelled)
{
    ObjectEvaluation result;
    QHash<int, PlotSeriesDataPtr> sources;
    for (const auto &source : snapshot.series) sources.insert(source->id, source);
    qsizetype retained = 0;
    for (const auto &object : objects) {
        QHash<QString, PlotSeriesDataPtr> bindings;
        for (const auto &field : object.fields) bindings.insert(field.id, sources.value(field.series));
        for (int ruleIndex = 0; ruleIndex < object.rules.size(); ++ruleIndex) {
            if (cancelled.load()) return {};
            const auto &rule = object.rules[ruleIndex];
            Q_ASSERT(rule.operation == "merge" && rule.outputs.size() == 1);
            QString error;
            QVector<PlotSeriesDataPtr> inputs;
            qsizetype count = 0;
            for (const auto &id : rule.inputs) {
                const auto source = bindings.value(id);
                if (!source || source->sampleCount() == 0) error = QStringLiteral("缺少有效来源");
                if (source) count += source->sampleCount();
                inputs.append(source);
            }
            if (count > (512 * 1024 * 1024 - retained) / 8) {
                error = QStringLiteral("派生结果超过 512 MiB 值缓存上限，请减少输出或数据规模");
                result.budgetLimitedRules.insert(rule.id);
            }
            if (error.isEmpty()) retained += count * 8;
            auto output = std::make_shared<PlotSeriesData>();
            output->id = rule.outputs[0].series; output->sourceFile = "object:" + object.id;
            output->sourceTable = ruleIndex; output->sourceColumn = 0;
            output->sourceTableName = object.name + "/" + rule.name;
            QVector<QPointF> samples;
            if (error.isEmpty()) {
                samples.reserve(count);
                for (const auto &source : inputs) {
                    for (qsizetype i = 0; i < source->sampleCount(); ++i) {
                        if ((i & 4095) == 0 && cancelled.load()) return {};
                        const auto point = source->pointAt(i);
                        if (!std::isfinite(point.x())) { error = QStringLiteral("合并来源含无效时间戳"); break; }
                        samples.append(point);
                    }
                    if (!error.isEmpty()) break;
                }
            }
            if (error.isEmpty()) {
                std::sort(samples.begin(), samples.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
                output->monotonicTime = true;
                for (qsizetype i = 0; i < samples.size(); ++i) {
                    if ((i & 4095) == 0 && cancelled.load()) return {};
                    const auto &point = samples[i];
                    const double value = std::isfinite(point.y()) ? point.y() : qQNaN();
                    if (!output->time.isEmpty() && output->time.last() == point.x()) {
                        double &previous = output->values.last();
                        if (std::isfinite(previous) && std::isfinite(value) && previous != value) {
                            error = QStringLiteral("同一时间戳存在不同值，无法合并：%1").arg(point.x(), 0, 'g', 17); break;
                        }
                        if (!std::isfinite(previous)) previous = value;
                    } else { output->time.append(point.x()); output->values.append(value); }
                }
            }
            if (!error.isEmpty()) { result.errors.insert(rule.id, error); output->time.clear(); output->values.clear(); }
            output->rangeIndex = PlotRangeIndex::build(*output, [&] { return cancelled.load(); });
            if (cancelled.load()) return {};
            bindings.insert(rule.outputs[0].id, output); result.series.append(output);
        }
    }
    return result;
}
