#include "xlsxwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamWriter>
#include <QtCore/private/qzipwriter_p.h>
#include <QtMath>

namespace {

constexpr qsizetype excelMaximumDataRows = 1048575;
constexpr int excelMaximumColumns = 16384;
constexpr qsizetype xmlBufferSize = 1024 * 1024;
constexpr int archiveProgressLimit = 95;

struct SheetPlan
{
    const XlsxExportTable *table = nullptr;
    QString name;
    qsizetype firstRow = 0;
    qsizetype rowCount = 0;
};

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
        report(qBound(0, int(fraction * archiveProgressLimit),
                      archiveProgressLimit));
    }

    void reportCopy(qint64 completed, qint64 total)
    {
        const qint64 normalizedTotal = qMax<qint64>(1, total);
        report(archiveProgressLimit
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

class ProgressReadDevice final : public QIODevice
{
public:
    ProgressReadDevice(QIODevice *source,
                       const std::function<void(qint64, qint64)> &progress,
                       const std::function<bool()> &isCancelled)
        : m_source(source), m_progress(progress), m_isCancelled(isCancelled),
          m_total(source ? source->size() : 0)
    {
        open(QIODevice::ReadOnly);
    }

    bool isSequential() const override { return true; }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (!m_source || (m_isCancelled && m_isCancelled())) return 0;
        const qint64 count = m_source->read(data, maxSize);
        if (count > 0) {
            m_processed += count;
            if (m_progress) m_progress(m_processed, m_total);
        }
        return count;
    }

    qint64 writeData(const char *, qint64) override { return -1; }

private:
    QIODevice *m_source = nullptr;
    std::function<void(qint64, qint64)> m_progress;
    std::function<bool()> m_isCancelled;
    qint64 m_total = 0;
    qint64 m_processed = 0;
};

QString columnName(int zeroBasedColumn)
{
    QString result;
    for (int column = zeroBasedColumn + 1; column > 0; column = (column - 1) / 26)
        result.prepend(QChar(QLatin1Char('A').unicode() + (column - 1) % 26));
    return result;
}

QString uniqueSheetName(QString requested, QSet<QString> *used)
{
    static const QString invalidCharacters = QStringLiteral("[]:*?/\\");
    for (QChar &character : requested)
        if (invalidCharacters.contains(character)) character = QLatin1Char('_');
    requested = requested.trimmed();
    while (requested.startsWith(QLatin1Char('\''))) requested.remove(0, 1);
    while (requested.endsWith(QLatin1Char('\''))) requested.chop(1);
    if (requested.isEmpty()) requested = QStringLiteral("Sheet");

    QString candidate = requested.left(31);
    int suffixNumber = 2;
    while (used->contains(candidate.toCaseFolded())) {
        const QString suffix = QStringLiteral(" (%1)").arg(suffixNumber++);
        candidate = requested.left(31 - suffix.size()) + suffix;
    }
    used->insert(candidate.toCaseFolded());
    return candidate;
}

QByteArray contentTypes(int sheetCount)
{
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("Types"));
    xml.writeDefaultNamespace(QStringLiteral(
        "http://schemas.openxmlformats.org/package/2006/content-types"));
    xml.writeEmptyElement(QStringLiteral("Default"));
    xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("rels"));
    xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral(
        "application/vnd.openxmlformats-package.relationships+xml"));
    xml.writeEmptyElement(QStringLiteral("Default"));
    xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("xml"));
    xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/xml"));
    xml.writeEmptyElement(QStringLiteral("Override"));
    xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/workbook.xml"));
    xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral(
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"));
    xml.writeEmptyElement(QStringLiteral("Override"));
    xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/styles.xml"));
    xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral(
        "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"));
    for (int index = 1; index <= sheetCount; ++index) {
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"),
                           QStringLiteral("/xl/worksheets/sheet%1.xml").arg(index));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral(
            "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"));
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return data;
}

QByteArray rootRelationships()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>");
}

QByteArray workbookXml(const QVector<SheetPlan> &sheets)
{
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("workbook"));
    xml.writeDefaultNamespace(QStringLiteral(
        "http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    xml.writeNamespace(QStringLiteral(
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                       QStringLiteral("r"));
    xml.writeStartElement(QStringLiteral("sheets"));
    for (int index = 0; index < sheets.size(); ++index) {
        xml.writeEmptyElement(QStringLiteral("sheet"));
        xml.writeAttribute(QStringLiteral("name"), sheets.at(index).name);
        xml.writeAttribute(QStringLiteral("sheetId"), QString::number(index + 1));
        xml.writeAttribute(QStringLiteral(
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships"),
                           QStringLiteral("id"), QStringLiteral("rId%1").arg(index + 1));
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return data;
}

QByteArray workbookRelationships(int sheetCount)
{
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("Relationships"));
    xml.writeDefaultNamespace(QStringLiteral(
        "http://schemas.openxmlformats.org/package/2006/relationships"));
    for (int index = 1; index <= sheetCount; ++index) {
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(index));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral(
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"));
        xml.writeAttribute(QStringLiteral("Target"),
                           QStringLiteral("worksheets/sheet%1.xml").arg(index));
    }
    xml.writeEmptyElement(QStringLiteral("Relationship"));
    xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(sheetCount + 1));
    xml.writeAttribute(QStringLiteral("Type"), QStringLiteral(
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"));
    xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("styles.xml"));
    xml.writeEndElement();
    xml.writeEndDocument();
    return data;
}

QByteArray stylesXml()
{
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
        "</styleSheet>");
}

void writeInlineCell(QXmlStreamWriter &xml, int column, qsizetype row,
                     const QString &value)
{
    xml.writeStartElement(QStringLiteral("c"));
    xml.writeAttribute(QStringLiteral("r"),
                       columnName(column) + QString::number(row));
    xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
    xml.writeStartElement(QStringLiteral("is"));
    xml.writeTextElement(QStringLiteral("t"), value);
    xml.writeEndElement();
    xml.writeEndElement();
}

void appendNumberCell(QByteArray *xml, const QByteArray &column,
                      const QByteArray &row, double value)
{
    xml->append("<c r=\"");
    xml->append(column);
    xml->append(row);
    xml->append("\"><v>");
    xml->append(QByteArray::number(value, 'g', 17));
    xml->append("</v></c>");
}

bool writeWorksheet(const QString &path, const SheetPlan &sheet,
                    const std::function<void(qsizetype)> &reportRows,
                    const std::function<bool()> &isCancelled,
                    QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法创建临时工作表：%1").arg(file.errorString());
        return false;
    }
    QByteArray xml;
    xml.reserve(xmlBufferSize + 4096);
    xml.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
               "<worksheet xmlns=\"http://schemas.openxmlformats.org/"
               "spreadsheetml/2006/main\"><sheetData>");
    QByteArray header;
    QXmlStreamWriter headerWriter(&header);
    headerWriter.writeStartElement(QStringLiteral("row"));
    headerWriter.writeAttribute(QStringLiteral("r"), QStringLiteral("1"));
    writeInlineCell(headerWriter, 0, 1, QStringLiteral("Time"));
    for (int column = 0; column < sheet.table->series.size(); ++column)
        writeInlineCell(headerWriter, column + 1, 1,
                        sheet.table->series.at(column).name);
    headerWriter.writeEndElement();
    xml.append(header);

    QVector<QByteArray> columns;
    columns.reserve(sheet.table->series.size() + 1);
    for (int column = 0; column <= sheet.table->series.size(); ++column)
        columns.append(columnName(column).toLatin1());
    auto flush = [&]() {
        if (xml.isEmpty()) return true;
        if (file.write(xml) != xml.size()) {
            *error = QStringLiteral("写入临时工作表失败：%1")
                         .arg(file.errorString());
            return false;
        }
        xml.clear();
        return true;
    };

    const PlotSeriesDataPtr timeSeries = sheet.table->series.first().data;
    for (qsizetype offset = 0; offset < sheet.rowCount; ++offset) {
        if ((offset & 0xff) == 0 && isCancelled && isCancelled()) return false;
        const qsizetype sourceRow = sheet.firstRow + offset;
        const qsizetype excelRow = offset + 2;
        const QByteArray row = QByteArray::number(excelRow);
        xml.append("<row r=\"");
        xml.append(row);
        xml.append("\">");
        const QPointF timePoint = timeSeries->pointAt(sourceRow);
        if (qIsFinite(timePoint.x()))
            appendNumberCell(&xml, columns.first(), row, timePoint.x());
        for (int column = 0; column < sheet.table->series.size(); ++column) {
            const PlotSeriesDataPtr data = sheet.table->series.at(column).data;
            if (!data || sourceRow >= data->sampleCount()) continue;
            const double value = data->pointAt(sourceRow).y();
            if (qIsFinite(value))
                appendNumberCell(&xml, columns.at(column + 1), row, value);
        }
        xml.append("</row>");
        if (xml.size() >= xmlBufferSize && !flush()) return false;
        if (reportRows && (((offset + 1) & 0xff) == 0
                           || offset + 1 == sheet.rowCount))
            reportRows(offset + 1);
    }
    xml.append("</sheetData></worksheet>");
    if (!flush()) return false;
    file.close();
    if (file.error() != QFile::NoError) {
        *error = QStringLiteral("写入临时工作表失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace

XlsxWriteResult writeXlsxWorkbook(
    const QString &path, const QVector<XlsxExportTable> &tables,
    const XlsxWriteOptions &options,
    const std::function<void(int)> &reportProgress,
    const std::function<bool()> &isCancelled)
{
    XlsxWriteResult result;
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
    const qsizetype rowsPerSheet = qBound<qsizetype>(
        1, options.maxDataRowsPerSheet, excelMaximumDataRows);
    QVector<SheetPlan> sheets;
    QSet<QString> usedNames;
    long double totalWork = 0.0L;
    for (const XlsxExportTable &table : tables) {
        if (table.series.isEmpty() || !table.series.first().data) continue;
        if (table.series.size() + 1 > excelMaximumColumns) {
            result.error = QStringLiteral("数据表 %1 超过 Excel 的列数上限").arg(table.name);
            return result;
        }
        const qsizetype rowCount = table.series.first().data->sampleCount();
        for (qsizetype first = 0; first < rowCount; first += rowsPerSheet) {
            sheets.append({&table, uniqueSheetName(table.name, &usedNames), first,
                           qMin(rowsPerSheet, rowCount - first)});
            totalWork += static_cast<long double>(sheets.last().rowCount)
                * (table.series.size() + 1);
        }
    }
    if (sheets.isEmpty()) {
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

    QFile archiveFile(temporary.filePath(QStringLiteral("workbook.xlsx")));
    if (!archiveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("无法创建临时 Excel 文件：%1")
                           .arg(archiveFile.errorString());
        return result;
    }
    QZipWriter archive(&archiveFile);
    archive.setCompressionPolicy(options.zipCompressionEnabled
                                     ? QZipWriter::AlwaysCompress
                                     : QZipWriter::NeverCompress);
    archive.addFile(QStringLiteral("[Content_Types].xml"), contentTypes(sheets.size()));
    archive.addFile(QStringLiteral("_rels/.rels"), rootRelationships());
    archive.addFile(QStringLiteral("xl/workbook.xml"), workbookXml(sheets));
    archive.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"),
                    workbookRelationships(sheets.size()));
    archive.addFile(QStringLiteral("xl/styles.xml"), stylesXml());

    long double completedWork = 0.0L;
    const long double worksheetGenerationShare = options.zipCompressionEnabled
        ? 0.35L : 0.85L;
    const QString worksheetPath = temporary.filePath(QStringLiteral("sheet.xml"));
    for (int index = 0; index < sheets.size(); ++index) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            archive.close();
            return result;
        }
        const SheetPlan &sheet = sheets.at(index);
        const long double sheetWork = static_cast<long double>(sheet.rowCount)
            * (sheet.table->series.size() + 1);
        if (!writeWorksheet(
                worksheetPath, sheet,
                [&](qsizetype rowsWritten) {
                    const long double fraction = sheet.rowCount > 0
                        ? static_cast<long double>(rowsWritten) / sheet.rowCount
                        : 1.0L;
                    progress.reportWork(
                        completedWork + sheetWork * worksheetGenerationShare
                            * fraction,
                        totalWork);
                },
                isCancelled, &result.error)) {
            result.cancelled = result.error.isEmpty() && isCancelled && isCancelled();
            archive.close();
            return result;
        }
        QFile worksheet(worksheetPath);
        if (!worksheet.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取临时工作表：%1").arg(worksheet.errorString());
            archive.close();
            return result;
        }
        ProgressReadDevice progressDevice(
            &worksheet,
            [&](qint64 bytesRead, qint64 byteCount) {
                const long double fraction = byteCount > 0
                    ? static_cast<long double>(bytesRead) / byteCount : 1.0L;
                progress.reportWork(
                    completedWork + sheetWork
                        * (worksheetGenerationShare
                           + (1.0L - worksheetGenerationShare) * fraction),
                    totalWork);
            },
            isCancelled);
        archive.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(index + 1),
                        &progressDevice);
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            archive.close();
            return result;
        }
        completedWork += sheetWork;
        progress.reportWork(completedWork, totalWork);
    }
    archive.close();
    if (archive.status() != QZipWriter::NoError) {
        result.error = QStringLiteral("写入 Excel 压缩包失败");
        return result;
    }
    if (isCancelled && isCancelled()) {
        result.cancelled = true;
        return result;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("无法创建 Excel 文件：%1").arg(output.errorString());
        return result;
    }
    if (!archiveFile.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法读取临时 Excel 文件：%1")
                           .arg(archiveFile.errorString());
        output.cancelWriting();
        return result;
    }
    const qint64 archiveSize = archiveFile.size();
    qint64 copiedBytes = 0;
    while (!archiveFile.atEnd()) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            output.cancelWriting();
            return result;
        }
        const QByteArray block = archiveFile.read(1024 * 1024);
        if (block.isEmpty() && archiveFile.error() != QFile::NoError) {
            result.error = QStringLiteral("读取临时 Excel 文件失败：%1")
                               .arg(archiveFile.errorString());
            output.cancelWriting();
            return result;
        }
        if (output.write(block) != block.size()) {
            result.error = QStringLiteral("写入 Excel 文件失败：%1")
                               .arg(output.errorString());
            output.cancelWriting();
            return result;
        }
        copiedBytes += block.size();
        progress.reportCopy(copiedBytes, archiveSize);
    }
    if (!output.commit()) {
        result.error = QStringLiteral("无法保存 Excel 文件：%1").arg(output.errorString());
        return result;
    }
    progress.report(100);
    return result;
}
