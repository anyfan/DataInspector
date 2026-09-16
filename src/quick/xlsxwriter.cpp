#include "xlsxwriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QXmlStreamWriter>
#include <QtCore/private/qzipwriter_p.h>
#include <QtMath>

namespace {

constexpr qsizetype excelMaximumDataRows = 1048575;
constexpr int excelMaximumColumns = 16384;

struct SheetPlan
{
    const XlsxExportTable *table = nullptr;
    QString name;
    qsizetype firstRow = 0;
    qsizetype rowCount = 0;
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

void writeNumberCell(QXmlStreamWriter &xml, int column, qsizetype row,
                     double value)
{
    xml.writeStartElement(QStringLiteral("c"));
    xml.writeAttribute(QStringLiteral("r"),
                       columnName(column) + QString::number(row));
    xml.writeTextElement(QStringLiteral("v"), QString::number(value, 'g', 17));
    xml.writeEndElement();
}

bool writeWorksheet(const QString &path, const SheetPlan &sheet,
                    qsizetype *processedRows, qsizetype totalRows,
                    const std::function<void(int)> &reportProgress,
                    const std::function<bool()> &isCancelled,
                    QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("无法创建临时工作表：%1").arg(file.errorString());
        return false;
    }
    QXmlStreamWriter xml(&file);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("worksheet"));
    xml.writeDefaultNamespace(QStringLiteral(
        "http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    xml.writeStartElement(QStringLiteral("sheetData"));
    xml.writeStartElement(QStringLiteral("row"));
    xml.writeAttribute(QStringLiteral("r"), QStringLiteral("1"));
    writeInlineCell(xml, 0, 1, QStringLiteral("Time"));
    for (int column = 0; column < sheet.table->series.size(); ++column)
        writeInlineCell(xml, column + 1, 1, sheet.table->series.at(column).name);
    xml.writeEndElement();

    const PlotSeriesDataPtr timeSeries = sheet.table->series.first().data;
    for (qsizetype offset = 0; offset < sheet.rowCount; ++offset) {
        if ((offset & 0xff) == 0 && isCancelled && isCancelled()) return false;
        const qsizetype sourceRow = sheet.firstRow + offset;
        const qsizetype excelRow = offset + 2;
        xml.writeStartElement(QStringLiteral("row"));
        xml.writeAttribute(QStringLiteral("r"), QString::number(excelRow));
        const QPointF timePoint = timeSeries->pointAt(sourceRow);
        if (qIsFinite(timePoint.x())) writeNumberCell(xml, 0, excelRow, timePoint.x());
        for (int column = 0; column < sheet.table->series.size(); ++column) {
            const PlotSeriesDataPtr data = sheet.table->series.at(column).data;
            if (!data || sourceRow >= data->sampleCount()) continue;
            const double value = data->pointAt(sourceRow).y();
            if (qIsFinite(value)) writeNumberCell(xml, column + 1, excelRow, value);
        }
        xml.writeEndElement();
        ++*processedRows;
        if (reportProgress && ((*processedRows & 0x3ff) == 0 || *processedRows == totalRows))
            reportProgress(qBound(0, int(*processedRows * 90 / qMax<qsizetype>(1, totalRows)), 90));
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
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
    const qsizetype rowsPerSheet = qBound<qsizetype>(
        1, options.maxDataRowsPerSheet, excelMaximumDataRows);
    QVector<SheetPlan> sheets;
    QSet<QString> usedNames;
    qsizetype totalRows = 0;
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
            totalRows += sheets.last().rowCount;
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
    archive.setCompressionPolicy(QZipWriter::AutoCompress);
    archive.addFile(QStringLiteral("[Content_Types].xml"), contentTypes(sheets.size()));
    archive.addFile(QStringLiteral("_rels/.rels"), rootRelationships());
    archive.addFile(QStringLiteral("xl/workbook.xml"), workbookXml(sheets));
    archive.addFile(QStringLiteral("xl/_rels/workbook.xml.rels"),
                    workbookRelationships(sheets.size()));
    archive.addFile(QStringLiteral("xl/styles.xml"), stylesXml());

    qsizetype processedRows = 0;
    const QString worksheetPath = temporary.filePath(QStringLiteral("sheet.xml"));
    for (int index = 0; index < sheets.size(); ++index) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            archive.close();
            return result;
        }
        if (!writeWorksheet(worksheetPath, sheets.at(index), &processedRows,
                            totalRows, reportProgress, isCancelled, &result.error)) {
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
        archive.addFile(QStringLiteral("xl/worksheets/sheet%1.xml").arg(index + 1),
                        &worksheet);
        if (reportProgress)
            reportProgress(90 + (index + 1) * 9 / sheets.size());
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
    }
    if (!output.commit()) {
        result.error = QStringLiteral("无法保存 Excel 文件：%1").arg(output.errorString());
        return result;
    }
    if (reportProgress) reportProgress(100);
    return result;
}
