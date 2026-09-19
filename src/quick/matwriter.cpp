#include "matwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
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

    void reportCopy(qint64 completed, qint64 total)
    {
        const qint64 normalizedTotal = qMax<qint64>(1, total);
        report(writeProgressLimit
               + int(qBound<qint64>(0, completed, normalizedTotal) * 4
                     / normalizedTotal));
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

struct MatFileHandle
{
    mat_t *file = nullptr;
    ~MatFileHandle() { close(); }
    void close()
    {
        if (file) Mat_Close(file);
        file = nullptr;
    }
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

bool writeVariable(mat_t *file, matvar_t *variable, const QByteArray &name,
                   QString *error)
{
    if (!variable) {
        *error = QStringLiteral("无法创建 MAT 变量 %1").arg(QString::fromLatin1(name));
        return false;
    }
    const int status = Mat_VarWrite(file, variable, MAT_COMPRESSION_NONE);
    Mat_VarFree(variable);
    if (status != 0) {
        *error = QStringLiteral("写入 MAT 变量 %1 失败").arg(QString::fromLatin1(name));
        return false;
    }
    return true;
}

bool writeDoubleMatrix(mat_t *file, const QByteArray &name, size_t rows,
                       size_t columns, double *values, QString *error)
{
    size_t dims[2] = {rows, columns};
    matvar_t *variable = Mat_VarCreate(name.constData(), MAT_C_DOUBLE, MAT_T_DOUBLE,
                                       2, dims, values, MAT_F_DONT_COPY_DATA);
    return writeVariable(file, variable, name, error);
}

bool writeCharMatrix(mat_t *file, const QByteArray &name, const QStringList &rows,
                     QString *error)
{
    size_t columns = 0;
    QByteArray bytes = encodeCharMatrix(rows, &columns);
    size_t dims[2] = {static_cast<size_t>(rows.size()), columns};
    // matio counts the UTF-8 bytes of rows * columns characters itself.
    matvar_t *variable = Mat_VarCreate(name.constData(), MAT_C_CHAR, MAT_T_UTF8,
                                       2, dims, bytes.data(), 0);
    return writeVariable(file, variable, name, error);
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
        const quint64 bytes = quint64(rowCount) * quint64(table.series.size() + 1)
            * sizeof(double);
        if (bytes > matVariableByteLimit) {
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
    QTemporaryDir temporary(outputDirectory.filePath(
        QStringLiteral(".datainspector-export-XXXXXX")));
    if (!temporary.isValid()) {
        result.error = QStringLiteral("无法创建导出临时目录");
        return result;
    }
    const QString temporaryPath = temporary.filePath(QStringLiteral("export.mat"));

    MatFileHandle mat;
    // matio takes UTF-8 paths on Windows, matching the loader.
    mat.file = Mat_CreateVer(temporaryPath.toUtf8().constData(), nullptr, MAT_FT_MAT5);
    if (!mat.file) {
        result.error = QStringLiteral("无法创建 MAT 文件：%1").arg(temporaryPath);
        return result;
    }

    long double completedWork = 0.0L;
    // Filling the column-major buffer is the part that can report progress;
    // matio then writes the whole variable in one go.
    constexpr long double bufferShare = 0.6L;
    QVector<double> buffer;
    for (const TablePlan &plan : std::as_const(plans)) {
        const DataExportTable &table = *plan.table;
        const qsizetype columns = table.series.size() + 1;
        const long double tableWork = static_cast<long double>(plan.rowCount) * columns;
        buffer.resize(plan.rowCount * columns);
        const PlotSeriesDataPtr timeSeries = table.series.first().data;
        for (qsizetype row = 0; row < plan.rowCount; ++row) {
            if ((row & 0xff) == 0 && isCancelled && isCancelled()) {
                result.cancelled = true;
                return result;
            }
            const double timestamp = timeSeries->pointAt(row).x();
            buffer[row] = qIsFinite(timestamp) ? timestamp : qQNaN();
            for (qsizetype column = 0; column < table.series.size(); ++column) {
                const PlotSeriesDataPtr data = table.series.at(column).data;
                const double value = data && row < data->sampleCount()
                    ? data->pointAt(row).y() : qQNaN();
                buffer[(column + 1) * plan.rowCount + row] =
                    qIsFinite(value) ? value : qQNaN();
            }
            if (((row + 1) & 0xfff) == 0) {
                progress.reportWork(
                    completedWork + tableWork * bufferShare * (row + 1) / plan.rowCount,
                    totalWork);
            }
        }
        progress.reportWork(completedWork + tableWork * bufferShare, totalWork);

        const QByteArray dataName = QByteArrayLiteral("p") + QByteArray::number(plan.number);
        if (!writeDoubleMatrix(mat.file, dataName, static_cast<size_t>(plan.rowCount),
                               static_cast<size_t>(columns), buffer.data(),
                               &result.error)) {
            return result;
        }
        QStringList titles;
        titles.reserve(columns);
        titles.append(QStringLiteral("Time"));
        for (const DataExportSeries &series : table.series) titles.append(series.name);
        if (!writeCharMatrix(mat.file, dataName + QByteArrayLiteral("_title"), titles,
                             &result.error)) {
            return result;
        }
        completedWork += tableWork;
        progress.reportWork(completedWork, totalWork);
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            return result;
        }
    }
    mat.close();

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("无法创建 MAT 文件：%1").arg(output.errorString());
        return result;
    }
    QFile written(temporaryPath);
    if (!written.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取临时 MAT 文件：%1").arg(written.errorString());
        output.cancelWriting();
        return result;
    }
    const qint64 totalBytes = written.size();
    qint64 copiedBytes = 0;
    while (!written.atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            output.cancelWriting();
            return result;
        }
        const QByteArray block = written.read(1024 * 1024);
        if (block.isEmpty() && written.error() != QFile::NoError) {
            result.error = QStringLiteral("读取临时 MAT 文件失败：%1").arg(written.errorString());
            output.cancelWriting();
            return result;
        }
        if (output.write(block) != block.size()) {
            result.error = QStringLiteral("写入 MAT 文件失败：%1").arg(output.errorString());
            output.cancelWriting();
            return result;
        }
        copiedBytes += block.size();
        progress.reportCopy(copiedBytes, totalBytes);
    }
    if (!output.commit()) {
        result.error = QStringLiteral("无法保存 MAT 文件：%1").arg(output.errorString());
        return result;
    }
    progress.report(100);
    return result;
#endif
}
