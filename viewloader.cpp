#include "viewloader.h"
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"
#include <QDebug>

bool ViewLoader::loadFromZip(const QString &path, ViewData &outData)
{
    if (path.isEmpty())
        return false;

    QuaZip zip(path);
    if (!zip.open(QuaZip::mdUnzip))
    {
        qWarning() << "ViewLoader: Could not open zip archive:" << path;
        return false;
    }

    QDomDocument viewMetaDataDoc;
    QDomDocument checkedSignalsDoc;
    bool foundViewMeta = false;
    bool foundCheckedSignals = false;

    QStringList allFiles = zip.getFileNameList();

    for (const QString &fileName : allFiles)
    {
        if (fileName != "views/sdi_view_meta_data.xml" && fileName != "views/sdi_checked_signals.xml")
            continue;

        if (!zip.setCurrentFile(fileName))
            continue;

        QuaZipFile zFile(&zip);
        if (!zFile.open(QIODevice::ReadOnly))
            continue;

        QByteArray xmlData = zFile.readAll();
        zFile.close();

        QDomDocument doc;
        if (doc.setContent(xmlData))
        {
            if (fileName == "views/sdi_view_meta_data.xml")
            {
                viewMetaDataDoc = doc;
                foundViewMeta = true;
            }
            else if (fileName == "views/sdi_checked_signals.xml")
            {
                checkedSignalsDoc = doc;
                foundCheckedSignals = true;
            }
        }
    }
    zip.close();

    if (!foundViewMeta || !foundCheckedSignals)
    {
        qWarning() << "ViewLoader: Missing required XML files in archive.";
        return false;
    }

    // 填充输出数据
    outData.layout = parseViewMetaData(viewMetaDataDoc);
    outData.signalList = parseCheckedSignals(checkedSignalsDoc);

    return true;
}

ViewLayoutInfo ViewLoader::parseViewMetaData(const QDomDocument &doc)
{
    ViewLayoutInfo info;
    QDomElement root = doc.documentElement();

    QDomElement rowsEl = root.firstChildElement("SubPlotRows");
    if (!rowsEl.isNull())
        info.rows = rowsEl.text().toInt();

    QDomElement colsEl = root.firstChildElement("SubPlotCols");
    if (!colsEl.isNull())
        info.cols = colsEl.text().toInt();

    QDomElement layoutEl = root.firstChildElement("LayoutType");
    if (!layoutEl.isNull())
        info.layoutType = layoutEl.text();

    return info;
}

QList<ViewSignalInfo> ViewLoader::parseCheckedSignals(const QDomDocument &doc)
{
    QList<ViewSignalInfo> signalList;
    QDomElement root = doc.documentElement();
    QDomElement signalsNode = root.firstChildElement("Signals");

    if (signalsNode.isNull())
        return signalList;

    QDomElement sigEl = signalsNode.firstChildElement();
    while (!sigEl.isNull())
    {
        ViewSignalInfo sigInfo;
        sigInfo.name = sigEl.firstChildElement("SignalName").text();
        sigInfo.id = sigEl.firstChildElement("ID").text().toInt();

        QDomElement colorEl = sigEl.firstChildElement("Color");
        if (!colorEl.isNull())
        {
            qreal r = colorEl.firstChildElement("r").text().toDouble();
            qreal g = colorEl.firstChildElement("g").text().toDouble();
            qreal b = colorEl.firstChildElement("b").text().toDouble();
            sigInfo.color = QColor::fromRgbF(r, g, b);
        }

        QDomElement plotsEl = sigEl.firstChildElement("Plots");
        if (!plotsEl.isNull())
        {
            QDomNodeList plotIdNodes = plotsEl.elementsByTagName("Element");
            for (int i = 0; i < plotIdNodes.count(); ++i)
            {
                sigInfo.plotIds.append(plotIdNodes.at(i).toElement().text().toInt());
            }
        }
        signalList.append(sigInfo);
        sigEl = sigEl.nextSiblingElement();
    }
    return signalList;
}