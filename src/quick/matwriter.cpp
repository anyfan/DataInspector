#include "matwriter.h"
#include "mat5streamwriter.h"
#include <QtEndian>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <QtMath>

#include <limits>

#ifdef ENABLE_MAT
#include "matio.h"
#endif

namespace {

constexpr int writeProgressLimit = 95;
// MAT 5 stores a 32-bit byte count per variable.
constexpr quint64 matVariableByteLimit = 0x7fffffffULL;

// Assigns pN numbers: a table imported from a MAT table keeps its number when
// it is still free; every other table takes the smallest unused number.
QVector<int> assignTableNumbers(const QVector<DataExportTable> &tables)
{
    static const QRegularExpression matTableName(QStringLiteral("^p(\\d+)$"));
    QVector<int> numbers(tables.size(), 0);
    QSet<int> used;
    for (int table = 0; table < tables.size(); ++table) {
        const QString leaf = tables.at(table).name.section(QLatin1Char('/'), -1);
        const QRegularExpressionMatch match = matTableName.match(leaf);
        if (!match.hasMatch()) continue;
        bool ok = false;
        const int number = match.captured(1).toInt(&ok);
        if (!ok || number <= 0 || used.contains(number)) continue;
        numbers[table] = number;
        used.insert(number);
    }
    int next = 1;
    for (int table = 0; table < tables.size(); ++table) {
        if (numbers.at(table) > 0) continue;
        while (used.contains(next)) ++next;
        numbers[table] = next;
        used.insert(next);
    }
    return numbers;
}

#ifdef ENABLE_MAT
class ProgressReporter
{
public:
    explicit ProgressReporter(const std::function<void(int)> &callback)
        : m_callback(callback)
    {
    }

    void reportWork(long double completed, long double total)
    {
        const long double fraction = total > 0.0L ? completed / total : 1.0L;
        report(qBound(0, int(fraction * writeProgressLimit), writeProgressLimit));
    }

    void report(int percentage)
    {
        const int normalized = qBound(0, percentage, 100);
        if (!m_callback || normalized <= m_lastPercentage) return;
        m_lastPercentage = normalized;
        m_callback(normalized);
    }

private:
    const std::function<void(int)> &m_callback;
    int m_lastPercentage = -1;
};

// Encodes strings as a MATLAB char matrix with one row per string, padded
// with spaces to the longest string and emitted column by column in UTF-8.
// This is the exact inverse of readMatStrings() in dataloadworker.cpp.
QByteArray encodeCharMatrix(const QStringList &rows, size_t *columnCount)
{
    QVector<QList<uint>> codePoints;
    codePoints.reserve(rows.size());
    qsizetype widest = 1;
    for (const QString &row : rows) {
        codePoints.append(row.toUcs4());
        widest = qMax(widest, codePoints.last().size());
    }
    QByteArray bytes;
    bytes.reserve(rows.size() * widest);
    for (qsizetype column = 0; column < widest; ++column) {
        for (const QList<uint> &row : std::as_const(codePoints)) {
            const char32_t codePoint = column < row.size()
                ? static_cast<char32_t>(row.at(column)) : U' ';
            bytes.append(QString::fromUcs4(&codePoint, 1).toUtf8());
        }
    }
    *columnCount = static_cast<size_t>(widest);
    return bytes;
}

#endif

} // namespace

bool matExportSupported()
{
#ifdef ENABLE_MAT
    return true;
#else
    return false;
#endif
}

MatWriteResult writeMatFile(
    const QString &path, const QVector<DataExportTable> &tables,
    const std::function<void(int)> &reportProgress,
    const std::function<bool()> &isCancelled)
{
    MatWriteResult result;
#ifndef ENABLE_MAT
    Q_UNUSED(tables)
    Q_UNUSED(reportProgress)
    Q_UNUSED(isCancelled)
    result.error = path.isEmpty() ? QStringLiteral("导出路径为空")
                                  : QStringLiteral("当前版本未启用 MAT 支持");
    return result;
#else
    if (path.isEmpty()) {
        result.error = QStringLiteral("导出路径为空");
        return result;
    }
    const auto validation = validateExportTimeBases(tables, isCancelled);
    if (validation.cancelled || !validation.error.isEmpty()) {
        result.cancelled = validation.cancelled;
        result.error = validation.error;
        return result;
    }
    struct TablePlan
    {
        const DataExportTable *table = nullptr;
        int number = 0;
        qsizetype rowCount = 0;
    };
    QVector<TablePlan> plans;
    const QVector<int> numbers = assignTableNumbers(tables);
    long double totalWork = 0.0L;
    for (int index = 0; index < tables.size(); ++index) {
        const DataExportTable &table = tables.at(index);
        if (table.series.isEmpty() || !table.series.first().data) continue;
        const qsizetype rowCount = table.series.first().data->sampleCount();
        if (rowCount <= 0) continue;
        const quint64 columns = quint64(table.series.size()) + 1;
        if (columns > quint64(std::numeric_limits<qint32>::max())
            || quint64(rowCount) > (matVariableByteLimit - 128) / sizeof(double) / columns) {
            result.error = QStringLiteral("数据表 %1 超过 MAT 5 格式的单变量 2 GB 上限")
                               .arg(table.name);
            return result;
        }
        plans.append({&table, numbers.at(index), rowCount});
        totalWork += static_cast<long double>(rowCount) * (table.series.size() + 1);
    }
    if (plans.isEmpty()) {
        result.error = QStringLiteral("没有可导出的数据");
        return result;
    }
    if (isCancelled && isCancelled()) {
        result.cancelled = true;
        return result;
    }
    ProgressReporter progress(reportProgress);
    progress.report(0);

    const QFileInfo outputInfo(path);
    QDir outputDirectory(outputInfo.absolutePath());
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("无法创建导出目录：%1").arg(outputDirectory.path());
        return result;
    }
    QSaveFile output(path);
    // Do not enable direct-write fallback: cancellation must preserve the old file.
    if (!output.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("无法创建 MAT 文件：%1").arg(output.errorString());
        return result;
    }
    Mat5StreamWriter writer(output);
    auto writeFailed = [&]() {
        result.error = writer.error();
        output.cancelWriting();
        return result;
    };
    auto cancelled = [&]() {
        if (!isCancelled || !isCancelled()) return false;
        result.cancelled = true;
        output.cancelWriting();
        return true;
    };
    if (cancelled()) return result;
    if (!writer.writeHeader()) return writeFailed();

    constexpr qsizetype chunkSamples = 8192; // 64 KiB, independent of table size
    QByteArray buffer;
    buffer.reserve(chunkSamples * qsizetype(sizeof(double)));
    long double completedWork = 0.0L;
    for (const TablePlan &plan : std::as_const(plans)) {
        const auto &table = *plan.table;
        const qsizetype columns = table.series.size() + 1;
        const quint64 bytes = quint64(plan.rowCount) * quint64(columns) * sizeof(double);
        const QByteArray dataName = QByteArrayLiteral("p") + QByteArray::number(plan.number);
        if (cancelled()) return result;
        if (!writer.beginMatrix(dataName, plan.rowCount, columns, MAT_C_DOUBLE, MAT_T_DOUBLE, bytes))
            return writeFailed();
        // MATLAB stores columns consecutively: only one chunk of one column is buffered.
        for (qsizetype column = 0; column < columns; ++column) {
            const auto &data = table.series.at(column == 0 ? 0 : column - 1).data;
            for (qsizetype first = 0; first < plan.rowCount; first += chunkSamples) {
                if (cancelled()) return result;
                const qsizetype count = qMin(chunkSamples, plan.rowCount - first);
                buffer.resize(count * qsizetype(sizeof(double)));
                for (qsizetype row = 0; row < count; ++row) {
                    const auto point = data->pointAt(first + row);
                    double value = column == 0 ? point.x() : point.y();
                    if (!qIsFinite(value)) value = qQNaN();
                    quint64 bits;
                    static_assert(sizeof(value) == sizeof(bits));
                    std::memcpy(&bits, &value, sizeof(bits));
                    qToLittleEndian(bits, buffer.data() + row * sizeof(double));
                }
                if (!writer.writePayload(buffer)) return writeFailed();
                completedWork += count;
                progress.reportWork(completedWork, totalWork);
            }
        }
        if (!writer.endMatrix()) return writeFailed();
        if (cancelled()) return result;
        QStringList titles{QStringLiteral("Time")};
        for (const auto &series : table.series) titles.append(series.name);
        size_t titleColumns = 0;
        const QByteArray titleBytes = encodeCharMatrix(titles, &titleColumns);
        if (!writer.beginMatrix(dataName + QByteArrayLiteral("_title"), titles.size(),
                                qsizetype(titleColumns), MAT_C_CHAR, MAT_T_UTF8, titleBytes.size()))
            return writeFailed();
        for (qsizetype first = 0; first < titleBytes.size(); first += chunkSamples * 8) {
            if (cancelled()) return result;
            if (!writer.writePayload(titleBytes.sliced(first,
                    qMin(chunkSamples * 8, titleBytes.size() - first)))) return writeFailed();
        }
        if (!writer.endMatrix()) return writeFailed();
    }
    progress.report(99);
    if (cancelled()) return result;
    if (!output.commit()) {
        result.error = QStringLiteral("无法保存 MAT 文件：%1").arg(output.errorString());
        return result;
    }
    progress.report(100);
    return result;
#endif
}
