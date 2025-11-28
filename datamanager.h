#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QList>
#include <QPen>

/**
 * @brief 存储一个单独的信号表 (来自 MAT 文件中的 pX)
 */
struct SignalTable
{
    QString name;        // 表名
    QStringList headers; // 信号头
    QVector<double> timeData;
    QVector<QVector<double>> valueData;
};
Q_DECLARE_METATYPE(SignalTable)

/**
 * @brief  数据容器
 */
struct FileData
{
    QString filePath; // 原始文件路径
    QList<SignalTable> tables;
};
Q_DECLARE_METATYPE(FileData)

/**
 * @brief 信号定位信息，用于在不同组件间传递信号引用
 */
struct SignalLocation
{
    const SignalTable *table = nullptr;
    int signalIndex = -1;
    QString name;
    QPen pen;
};

/**
 * @brief 数据管理器
 */
class DataManager : public QObject
{
    Q_OBJECT

public:
    explicit DataManager(QObject *parent = nullptr);

public slots:
    void loadCsvFile(const QString &filePath);
    void loadMatFile(const QString &filePath);

signals:
    void loadProgress(int percentage);
    void loadFinished(const FileData &data);
    void loadFailed(const QString &filePath, const QString &errorString);
};