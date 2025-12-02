#ifndef VIEWLOADER_H
#define VIEWLOADER_H

#include <QString>
#include <QList>
#include <QColor>
#include <QDomDocument>

struct ViewLayoutInfo
{
    int rows = 1;
    int cols = 1;
    QString layoutType;
};

struct ViewSignalInfo
{
    QString name;
    QString uniqueId;
    int id = 0;
    QColor color;
    QList<int> plotIds;
};

struct ViewData
{
    ViewLayoutInfo layout;
    QList<ViewSignalInfo> signalList;
};

class ViewLoader
{
public:
    /**
     * @brief 从 .mldatx (zip) 文件加载视图数据
     * @return 如果成功返回 ViewData，否则返回 std::nullopt
     */
    static bool loadFromZip(const QString &path, ViewData &outData);

    static bool saveToJson(const QString &path, const ViewData &data);

    static bool loadFromJson(const QString &path, ViewData &outData);

private:
    static ViewLayoutInfo parseViewMetaData(const QDomDocument &doc);
    static QList<ViewSignalInfo> parseCheckedSignals(const QDomDocument &doc);
};

#endif // VIEWLOADER_H