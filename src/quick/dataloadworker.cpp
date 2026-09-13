#include "dataloadworker.h"
#include "signalmetadata.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QStringView>
#include <QTextStream>
#include <QThread>
#include <QtMath>
#include <algorithm>
#include <limits>
#include <utility>

#ifdef ENABLE_MAT
#include "matio.h"
#endif

namespace {

#ifdef ENABLE_MAT
static int utf8CharLength(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

static QStringList readMatStrings(matvar_t *variable)
{
    QStringList result;
    if (!variable || !variable->data || variable->rank != 2) return result;
    const size_t rows = variable->dims[0], cols = variable->dims[1];
    if (!rows || !cols) return result;
    if (variable->data_type == MAT_T_UTF8) {
        QVector<QByteArray> buffers(static_cast<int>(rows));
        auto *scanner = static_cast<unsigned char *>(variable->data);
        for (size_t col = 0; col < cols; ++col) for (size_t row = 0; row < rows; ++row) {
            const int length = utf8CharLength(*scanner);
            buffers[static_cast<int>(row)].append(reinterpret_cast<const char *>(scanner), length);
            scanner += length;
        }
        for (const QByteArray &buffer : buffers) result.append(QString::fromUtf8(buffer).trimmed());
    } else if (variable->data_type == MAT_T_INT8 || variable->data_type == MAT_T_UINT8) {
        const auto *data = static_cast<const char *>(variable->data);
        for (size_t row = 0; row < rows; ++row) {
            QByteArray buffer;
            for (size_t col = 0; col < cols; ++col) { const char c = data[col * rows + row]; if (!c) break; buffer.append(c); }
            result.append(QString::fromLocal8Bit(buffer).trimmed());
        }
    }
    return result;
}

static bool buildMatTable(int index, matvar_t *dataVar, LoadedTable &table)
{
    const QString name = QStringLiteral("p%1").arg(index);
    if (!dataVar || dataVar->data_type != MAT_T_DOUBLE || dataVar->rank != 2 || dataVar->dims[1] < 2) return false;
    const size_t rows = dataVar->dims[0], cols = dataVar->dims[1];
    const int signalCount = static_cast<int>(cols - 1);
    table.name = name;
    const auto *raw = static_cast<const double *>(dataVar->data);
    table.time.resize(static_cast<int>(rows));
    bool monotonic = true;
    double previousTime = -std::numeric_limits<double>::infinity();
    for (size_t row = 0; row < rows; ++row) {
        const double timestamp = raw[row];
        table.time[static_cast<int>(row)] = timestamp;
        if (!qIsFinite(timestamp)) {
            monotonic = false;
            continue;
        }
        if (timestamp < previousTime) monotonic = false;
        previousTime = timestamp;
        if (!table.hasTimeBounds) {
            table.timeMinimum = table.timeMaximum = timestamp;
            table.hasTimeBounds = true;
        } else {
            table.timeMinimum = qMin(table.timeMinimum, timestamp);
            table.timeMaximum = qMax(table.timeMaximum, timestamp);
        }
    }
    table.values.resize(signalCount);
    for (int signal = 0; signal < signalCount; ++signal) {
        table.values[signal].resize(static_cast<int>(rows));
        const double *column = raw + (signal + 1) * rows;
        std::copy(column, column + rows, table.values[signal].begin());
    }
    table.monotonicTimes.fill(monotonic, signalCount);
    table.rowCount = static_cast<qsizetype>(rows);
    return true;
}
#endif

static QChar detectCsvDelimiter(QStringView line)
{
    QHash<QChar, int> counts;
    bool quoted = false;
    for (qsizetype index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size()
                && line.at(index + 1) == QLatin1Char('"')) {
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (!quoted && (character == QLatin1Char(',')
                              || character == QLatin1Char(';')
                              || character == QLatin1Char('\t'))) {
            ++counts[character];
        }
    }
    QChar delimiter = QLatin1Char(',');
    for (const QChar candidate : {QLatin1Char(','), QLatin1Char(';'),
                                  QLatin1Char('\t')}) {
        if (counts.value(candidate) > counts.value(delimiter))
            delimiter = candidate;
    }
    return delimiter;
}

static QStringList parseQuotedCsvFields(QStringView line, QChar delimiter)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (qsizetype index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size()
                && line.at(index + 1) == QLatin1Char('"')) {
                field.append(QLatin1Char('"'));
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == delimiter && !quoted) {
            fields.append(field.trimmed());
            field.clear();
        } else {
            field.append(character);
        }
    }
    fields.append(field.trimmed());
    return fields;
}

static QString csvFieldText(QStringView field)
{
    return field.toString().trimmed();
}

static QString csvFieldText(const QString &field)
{
    return field.trimmed();
}

} // namespace

void DataLoadWorker::loadFile(const QString &path)
{
    if (path.endsWith(QStringLiteral(".mat"), Qt::CaseInsensitive)) {
#ifdef ENABLE_MAT
        loadMat(path);
#else
        emit finished(path, {}, 0, QStringLiteral("当前构建未启用 MAT 支持，请使用 -DENABLE_MAT=ON 并提供兼容 LLVM-MinGW 的 matio/HDF5 库"));
#endif
        return;
    }
    loadCsv(path);
}

void DataLoadWorker::loadCsv(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { emit finished(path, {}, 0, QStringLiteral("无法打开文件：%1").arg(path)); return; }
    QTextStream stream(&file);
    if (stream.atEnd()) { emit finished(path, {}, 0, QStringLiteral("文件为空：%1").arg(path)); return; }
    const QString headerLine = stream.readLine();
    const QChar delimiter = detectCsvDelimiter(QStringView{headerLine});
    QStringList headers = parseQuotedCsvFields(QStringView{headerLine}, delimiter);
    if (!headers.isEmpty() && headers.first().startsWith(QChar::ByteOrderMark))
        headers[0].remove(0, 1);
    if (headers.size() < 2) { emit finished(path, {}, 0, QStringLiteral("CSV 至少需要时间列和一个信号列")); return; }
    LoadedTable table;
    table.name = QFileInfo(path).completeBaseName();
    table.values.resize(headers.size() - 1);
    table.monotonicTimes.fill(true, headers.size() - 1);
    for (int i = 1; i < headers.size(); ++i) { const QString n = headers.at(i).trimmed(); table.signalNames.append(n.isEmpty() ? QStringLiteral("Signal %1").arg(i) : n); }
    const qint64 fileSize = file.size();
    const qsizetype estimatedRows = fileSize > 0
        ? static_cast<qsizetype>(fileSize / qMax<qsizetype>(headerLine.size(), 50)) : 0;
    table.time.reserve(estimatedRows);
    for (QVector<double> &values : table.values) values.reserve(estimatedRows);
    int skipped = 0;
    int lineCount = 0;
    int lastProgress = 0;
    double previousTime = -std::numeric_limits<double>::infinity();
    emit progress(path, 0);
    while (!stream.atEnd()) {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        const QString line = stream.readLine();
        ++lineCount;
        if ((lineCount & 0xff) == 0 && fileSize > 0) {
            const int percentage = qBound(0, static_cast<int>(stream.pos() * 90 / fileSize), 90);
            if (percentage > lastProgress) {
                lastProgress = percentage;
                emit progress(path, percentage);
            }
        }
        if (line.trimmed().isEmpty()) continue;
        auto appendFields = [&](const auto &fields) {
            if (fields.size() != headers.size()) return false;
            bool timeOk = false;
            const double timestamp = csvFieldText(fields.at(0)).toDouble(&timeOk);
            if (!timeOk) return false;
            ++table.rowCount;
            table.time.append(timestamp);
            const bool finiteTime = qIsFinite(timestamp);
            if (!finiteTime) {
                for (int signal = 0; signal < table.values.size(); ++signal) {
                    table.values[signal].append(qQNaN());
                    table.monotonicTimes[signal] = false;
                }
                return true;
            }
            if (!table.hasTimeBounds) {
                table.timeMinimum = table.timeMaximum = timestamp;
                table.hasTimeBounds = true;
            } else {
                table.timeMinimum = qMin(table.timeMinimum, timestamp);
                table.timeMaximum = qMax(table.timeMaximum, timestamp);
            }
            if (timestamp < previousTime) {
                for (int signal = 0; signal < table.monotonicTimes.size(); ++signal)
                    table.monotonicTimes[signal] = false;
            }
            previousTime = timestamp;
            for (int signal = 0; signal < table.values.size(); ++signal) {
                bool valueOk = false;
                const double value = csvFieldText(fields.at(signal + 1)).toDouble(&valueOk);
                table.values[signal].append(
                    valueOk && qIsFinite(value) ? value : qQNaN());
            }
            return true;
        };
        bool accepted = false;
        if (line.contains(QLatin1Char('"'))) {
            accepted = appendFields(parseQuotedCsvFields(QStringView{line}, delimiter));
        } else {
            accepted = appendFields(QStringView{line}.split(delimiter));
        }
        if (!accepted) ++skipped;
    }
    if (table.rowCount == 0) { emit finished(path, {}, skipped, QStringLiteral("没有读取到有效数据：%1").arg(path)); return; }
    emit progress(path, 99);
    emit finished(path, {table}, skipped, {});
}

#ifdef ENABLE_MAT
void DataLoadWorker::loadMat(const QString &path)
{
    const QByteArray encoded = QFile::encodeName(path);
    mat_t *file = Mat_Open(encoded.constData(), MAT_ACC_RDONLY);
    if (!file) { emit finished(path, {}, 0, QStringLiteral("无法打开 MAT 文件：%1").arg(path)); return; }
    const QRegularExpression variableExpression(
        QStringLiteral("^p(\\d+)(?:_(title2?))?$"));
    QMap<int, LoadedTable> loadedTables;
    QMap<int, QStringList> firstTitles;
    QMap<int, QStringList> secondTitles;
    const qint64 fileSize = QFileInfo(path).size();
    qint64 processedDataBytes = 0;
    matvar_t *variable = nullptr;
    emit progress(path, 0);
    while ((variable = Mat_VarReadNextInfo(file)) != nullptr) {
        const QString name = QString::fromLatin1(variable->name ? variable->name : "");
        const QRegularExpressionMatch match = variableExpression.match(name);
        if (!match.hasMatch()) {
            Mat_VarFree(variable);
            continue;
        }
        const int index = match.captured(1).toInt();
        const QString suffix = match.captured(2);
        if (Mat_VarReadDataAll(file, variable) == 0) {
            if (suffix == QStringLiteral("title")) {
                firstTitles.insert(index, readMatStrings(variable));
            } else if (suffix == QStringLiteral("title2")) {
                secondTitles.insert(index, readMatStrings(variable));
            } else {
                LoadedTable table;
                if (buildMatTable(index, variable, table))
                    loadedTables.insert(index, std::move(table));
                processedDataBytes += static_cast<qint64>(variable->nbytes);
                if (fileSize > 0) {
                    emit progress(path, qBound(
                        1, static_cast<int>(processedDataBytes * 98 / fileSize),
                        98));
                }
            }
        }
        // The table and title strings now live in independent Qt buffers.
        // Never retain MATIO's raw matrix beyond this iteration.
        Mat_VarFree(variable);
    }
    Mat_Close(file);
    QVector<LoadedTable> tables;
    tables.reserve(loadedTables.size());
    for (auto iterator = loadedTables.begin(); iterator != loadedTables.end();
         ++iterator) {
        LoadedTable table = std::move(iterator.value());
        table.signalNames = composeMatSignalNames(
            firstTitles.value(iterator.key()),
            secondTitles.value(iterator.key()), table.values.size(),
            table.name);
        tables.append(std::move(table));
    }
    if (tables.isEmpty()) { emit finished(path, {}, 0, QStringLiteral("MAT 文件中没有有效的 pN 数据变量")); return; }
    emit progress(path, 99);
    emit finished(path, tables, 0, {});
}
#endif
