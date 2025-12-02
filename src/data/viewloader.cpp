#include "viewloader.h"
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

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

    // 更新 plotIds
    for (ViewSignalInfo &sig : outData.signalList)
    {
        // 遍历该信号应在的子图 ID
        for (int &sdiPlotId : sig.plotIds)
        {
            if (sdiPlotId < 1)
                sdiPlotId = 1;
            int r = (sdiPlotId - 1) % 8 + 1;                         // 1-based row
            int c = (sdiPlotId - 1) / 8 + 1;                         // 1-based col
            int plotIndex = (r - 1) * outData.layout.cols + (c - 1); // 映射到 grid index

            sdiPlotId = plotIndex;
        }
    }

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

bool ViewLoader::saveToJson(const QString &path, const ViewData &data)
{
    QJsonObject rootObj;

    // 1. 保存 Layout 信息
    QJsonObject layoutObj;
    layoutObj["rows"] = data.layout.rows;
    layoutObj["cols"] = data.layout.cols;
    layoutObj["type"] = data.layout.layoutType;
    rootObj["layout"] = layoutObj;

    // 2. 保存 Signals 信息
    QJsonArray signalsArray;
    for (const ViewSignalInfo &sig : data.signalList)
    {
        QJsonObject sigObj;
        sigObj["name"] = sig.name;
        sigObj["uniqueId"] = sig.uniqueId; // 保存 uniqueID

        // 保存颜色 (Hex 字符串)
        if (sig.color.isValid())
            sigObj["color"] = sig.color.name(QColor::HexArgb);

        // 保存 plotIds
        QJsonArray plotsArray;
        for (int pid : sig.plotIds)
            plotsArray.append(pid);

        sigObj["plots"] = plotsArray;
        signalsArray.append(sigObj);
    }
    rootObj["signals"] = signalsArray;

    // 3. 写入文件
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning() << "ViewLoader: Could not open file for writing:" << path;
        return false;
    }

    QJsonDocument doc(rootObj);
    file.write(doc.toJson());
    file.close();

    return true;
}

bool ViewLoader::loadFromJson(const QString &path, ViewData &outData)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "ViewLoader: Could not open file for reading:" << path;
        return false;
    }

    QByteArray bytes = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(bytes);
    if (doc.isNull())
    {
        qWarning() << "ViewLoader: Invalid JSON content.";
        return false;
    }

    QJsonObject rootObj = doc.object();

    // 1. 读取 Layout
    if (rootObj.contains("layout"))
    {
        QJsonObject layoutObj = rootObj["layout"].toObject();
        outData.layout.rows = layoutObj["rows"].toInt(1);
        outData.layout.cols = layoutObj["cols"].toInt(1);
        outData.layout.layoutType = layoutObj["type"].toString();
    }

    // 2. 读取 Signals
    if (rootObj.contains("signals"))
    {
        QJsonArray signalsArray = rootObj["signals"].toArray();
        for (const QJsonValue &val : signalsArray)
        {
            QJsonObject sigObj = val.toObject();
            ViewSignalInfo sigInfo;

            sigInfo.name = sigObj["name"].toString();
            sigInfo.uniqueId = sigObj["uniqueId"].toString();

            if (sigObj.contains("color"))
                sigInfo.color = QColor(sigObj["color"].toString());

            if (sigObj.contains("plots"))
            {
                QJsonArray plotsArr = sigObj["plots"].toArray();
                for (const QJsonValue &pVal : plotsArr)
                    sigInfo.plotIds.append(pVal.toInt());
            }

            outData.signalList.append(sigInfo);
        }
    }

    return true;
}