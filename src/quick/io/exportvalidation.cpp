#include "exporttable.h"

#include <QtMath>

ExportValidationResult validateExportTimeBases(
    const QVector<DataExportTable> &tables, const std::function<bool()> &isCancelled)
{
    for (const auto &table : tables) {
        if (isCancelled && isCancelled()) return {{}, true};
        if (table.series.isEmpty()) continue;
        const auto &reference = table.series.first().data;
        auto mismatch = [&] {
            return ExportValidationResult{
                QStringLiteral("数据表 %1 的信号时间基不一致，拒绝共用时间列").arg(table.name), false};
        };
        if (!reference) return mismatch();
        for (const auto &column : table.series) {
            const auto &data = column.data;
            if (!data || data->sampleCount() != reference->sampleCount()) return mismatch();
            if (data == reference) continue;
            // Imported columns share an immutable Qt time vector: constant-time proof.
            if (data->points.isEmpty() && reference->points.isEmpty()
                && data->time.constData() == reference->time.constData()
                && data->timeOffset == reference->timeOffset) continue;
            for (qsizetype row = 0; row < reference->sampleCount(); ++row) {
                if ((row & 0xfff) == 0 && isCancelled && isCancelled()) return {{}, true};
                const double a = reference->pointAt(row).x();
                const double b = data->pointAt(row).x();
                // Exact comparison: a fuzzy match could silently alter sampling times.
                if (a != b && !(qIsNaN(a) && qIsNaN(b))) return mismatch();
            }
        }
    }
    return {};
}
