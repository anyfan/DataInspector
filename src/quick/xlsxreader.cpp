#include "xlsxreader.h"

#include <QDir>
#include <QHash>
#include <QLocale>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>
#include <QtMath>

#include <limits>

namespace {

const QString relationshipNamespace = QStringLiteral(
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships");

struct WorksheetInfo
{
    QString name;
    QString relationshipId;
    QString path;
};

struct CellValue
{
    int column = -1;
    QString text;
    bool numeric = false;
    double number = 0.0;
};

int columnIndex(QStringView reference)
{
    int result = 0;
    bool found = false;
    for (QChar character : reference) {
        if (!character.isLetter()) break;
        const ushort upper = character.toUpper().unicode();
        if (upper < 'A' || upper > 'Z') return -1;
        result = result * 26 + (upper - 'A' + 1);
        found = true;
    }
    return found ? result - 1 : -1;
}

QString readTextContent(QXmlStreamReader &xml, QStringView endElement)
{
    QString text;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("t"))
            text += xml.readElementText();
        else if (xml.isEndElement() && xml.name() == endElement)
            break;
    }
    return text;
}

QStringList readSharedStrings(const QByteArray &data, QString *error)
{
    QStringList strings;
    if (data.isEmpty()) return strings;

    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("si"))
            strings.append(readTextContent(xml, QStringView{u"si"}));
    }
    if (xml.hasError() && error)
        *error = QStringLiteral("无法解析 Excel 共享字符串：%1")
                     .arg(xml.errorString());
    return strings;
}

QVector<WorksheetInfo> readWorkbook(const QByteArray &data, QString *error)
{
    QVector<WorksheetInfo> sheets;
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("sheet"))
            continue;
        WorksheetInfo sheet;
        sheet.name = xml.attributes().value(QStringLiteral("name")).toString();
        sheet.relationshipId = xml.attributes()
                                   .value(relationshipNamespace,
                                          QStringLiteral("id"))
                                   .toString();
        if (!sheet.name.isEmpty() && !sheet.relationshipId.isEmpty())
            sheets.append(std::move(sheet));
    }
    if (xml.hasError() && error)
        *error = QStringLiteral("无法解析 Excel 工作簿：%1")
                     .arg(xml.errorString());
    return sheets;
}

QHash<QString, QString> readRelationships(const QByteArray &data,
                                          QString *error)
{
    QHash<QString, QString> relationships;
    QXmlStreamReader xml(data);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()
            || xml.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto attributes = xml.attributes();
        const QString id = attributes.value(QStringLiteral("Id")).toString();
        QString target = attributes.value(QStringLiteral("Target")).toString();
        const QString type = attributes.value(QStringLiteral("Type")).toString();
        if (id.isEmpty() || !type.endsWith(QLatin1String("/worksheet")))
            continue;
        if (target.startsWith(QLatin1Char('/'))) target.remove(0, 1);
        else target.prepend(QStringLiteral("xl/"));
        target = QDir::cleanPath(target).replace(QLatin1Char('\\'),
                                                   QLatin1Char('/'));
        relationships.insert(id, target);
    }
    if (xml.hasError() && error)
        *error = QStringLiteral("无法解析 Excel 工作表关系：%1")
                     .arg(xml.errorString());
    return relationships;
}

CellValue readCell(QXmlStreamReader &xml, const QStringList &sharedStrings)
{
    CellValue cell;
    const auto attributes = xml.attributes();
    cell.column = columnIndex(attributes.value(QStringLiteral("r")));
    const QString type = attributes.value(QStringLiteral("t")).toString();
    QString raw;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("v")) {
            raw = xml.readElementText();
        } else if (xml.isStartElement()
                   && xml.name() == QLatin1String("is")) {
            raw = readTextContent(xml, QStringView{u"is"});
        } else if (xml.isEndElement() && xml.name() == QLatin1String("c")) {
            break;
        }
    }

    if (type == QLatin1String("s")) {
        bool ok = false;
        const int index = raw.toInt(&ok);
        if (ok && index >= 0 && index < sharedStrings.size())
            cell.text = sharedStrings.at(index);
        return cell;
    }
    if (type == QLatin1String("inlineStr") || type == QLatin1String("str")) {
        cell.text = raw;
        return cell;
    }
    if (type == QLatin1String("e") || raw.isEmpty()) return cell;

    bool ok = false;
    cell.number = QLocale::c().toDouble(QStringView{raw}, &ok);
    cell.numeric = ok;
    if (!ok) cell.text = raw;
    return cell;
}

QVector<CellValue> readRow(QXmlStreamReader &xml,
                           const QStringList &sharedStrings)
{
    QVector<CellValue> cells;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("c"))
            cells.append(readCell(xml, sharedStrings));
        else if (xml.isEndElement() && xml.name() == QLatin1String("row"))
            break;
    }
    return cells;
}

QString cellText(const CellValue &cell)
{
    return cell.numeric ? QString::number(cell.number, 'g', 16) : cell.text;
}

bool cellNumber(const CellValue &cell, double *number)
{
    if (cell.numeric) {
        *number = cell.number;
        return true;
    }
    bool ok = false;
    *number = QLocale::c().toDouble(QStringView{cell.text}.trimmed(), &ok);
    return ok;
}

bool appendWorksheetRow(LoadedTable &table, const QVector<CellValue> &cells,
                        int columnCount, double *previousTime)
{
    QVector<double> values(columnCount, qQNaN());
    QVector<bool> present(columnCount, false);
    for (const CellValue &cell : cells) {
        if (cell.column < 0 || cell.column >= columnCount) continue;
        present[cell.column] = cellNumber(cell, &values[cell.column]);
    }
    if (!present.first()) return false;

    const double timestamp = values.first();
    ++table.rowCount;
    table.time.append(timestamp);
    if (!qIsFinite(timestamp)) {
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
    if (timestamp < *previousTime)
        table.monotonicTimes.fill(false);
    *previousTime = timestamp;

    for (int signal = 0; signal < table.values.size(); ++signal) {
        const double value = values.at(signal + 1);
        table.values[signal].append(present.at(signal + 1)
                                        && qIsFinite(value)
                                    ? value
                                    : qQNaN());
    }
    return true;
}

bool readWorksheet(const QByteArray &data, const QString &name,
                   const QStringList &sharedStrings, LoadedTable *table,
                   int *skippedRows, const std::function<bool()> &isCancelled,
                   QString *error)
{
    QXmlStreamReader xml(data);
    bool headerRead = false;
    int columnCount = 0;
    double previousTime = -std::numeric_limits<double>::infinity();

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QLatin1String("row"))
            continue;
        if (isCancelled && isCancelled()) return false;

        QVector<CellValue> cells = readRow(xml, sharedStrings);
        if (!headerRead) {
            for (const CellValue &cell : cells)
                columnCount = qMax(columnCount, cell.column + 1);
            if (columnCount < 2) return false;

            table->name = name;
            table->signalNames.resize(columnCount - 1);
            for (const CellValue &cell : cells) {
                if (cell.column <= 0 || cell.column >= columnCount) continue;
                table->signalNames[cell.column - 1] = cellText(cell).trimmed();
            }
            for (int signal = 0; signal < table->signalNames.size(); ++signal) {
                if (table->signalNames.at(signal).isEmpty())
                    table->signalNames[signal] = QStringLiteral("Signal %1")
                                                     .arg(signal + 1);
            }
            table->values.resize(columnCount - 1);
            table->monotonicTimes.fill(true, columnCount - 1);
            headerRead = true;
            continue;
        }

        if (!appendWorksheetRow(*table, cells, columnCount, &previousTime))
            ++*skippedRows;
    }
    if (xml.hasError()) {
        if (error)
            *error = QStringLiteral("无法解析工作表 %1：%2")
                         .arg(name, xml.errorString());
        return false;
    }
    return headerRead && table->rowCount > 0;
}

} // namespace

XlsxReadResult readXlsxWorkbook(
    const QString &path, const std::function<void(int)> &reportProgress,
    const std::function<bool()> &isCancelled)
{
    XlsxReadResult result;
    QZipReader archive(path);
    if (!archive.exists() || !archive.isReadable()) {
        result.error = QStringLiteral("无法打开 Excel 文件：%1").arg(path);
        return result;
    }

    QString parseError;
    const QStringList sharedStrings = readSharedStrings(
        archive.fileData(QStringLiteral("xl/sharedStrings.xml")), &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }
    QVector<WorksheetInfo> sheets = readWorkbook(
        archive.fileData(QStringLiteral("xl/workbook.xml")), &parseError);
    const QHash<QString, QString> relationships = readRelationships(
        archive.fileData(QStringLiteral("xl/_rels/workbook.xml.rels")),
        &parseError);
    if (!parseError.isEmpty()) {
        result.error = parseError;
        return result;
    }

    for (WorksheetInfo &sheet : sheets)
        sheet.path = relationships.value(sheet.relationshipId);
    if (sheets.isEmpty()) {
        result.error = QStringLiteral("Excel 文件中没有工作表");
        return result;
    }

    for (int index = 0; index < sheets.size(); ++index) {
        if (isCancelled && isCancelled()) {
            result.cancelled = true;
            return result;
        }
        const WorksheetInfo &sheet = sheets.at(index);
        if (sheet.path.isEmpty()) continue;
        const QByteArray data = archive.fileData(sheet.path);
        if (data.isEmpty()) continue;

        LoadedTable table;
        if (readWorksheet(data, sheet.name, sharedStrings, &table,
                          &result.skippedRows, isCancelled, &parseError)) {
            result.tables.append(std::move(table));
        } else if (isCancelled && isCancelled()) {
            result.cancelled = true;
            return result;
        } else if (!parseError.isEmpty()) {
            result.error = parseError;
            return result;
        }
        if (reportProgress)
            reportProgress(qBound(1, (index + 1) * 98 / sheets.size(), 98));
    }

    if (result.tables.isEmpty())
        result.error = QStringLiteral("Excel 文件中没有有效的时间序列工作表");
    return result;
}
