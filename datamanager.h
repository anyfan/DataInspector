#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QList> // <-- 新增

/**
 * @brief [新增] 存储一个单独的信号表 (来自 MAT 文件中的 pX)
 * * CSV 文件将被视为一个只包含单个 SignalTable 的 FileData。
 */
struct SignalTable
{
    QString name;        // 表名 (例如 "p1" 或 "p1_title" 的内容)
    QStringList headers; // 信号头 (来自 "p1_title2")
    QVector<double> timeData;
    QVector<QVector<double>> valueData;
};
Q_DECLARE_METATYPE(SignalTable)

/**
 * @brief [修改] 这是一个新的数据容器，用于通过信号槽传递
 * * 它可以容纳来自 CSV 的单个表或来自 MAT 的多个表。
 */
struct FileData
{
    QString filePath; // 原始文件路径 (例如 "my_data.mat")
    QList<SignalTable> tables;
};
Q_DECLARE_METATYPE(FileData)

/**
 * @brief 数据管理器 (运行在工作线程中)
 * * 负责所有耗时的 I/O 和数据处理, 避免阻塞 GUI 线程。
 * 遵循 data flow.md 中的 Milestone 2 架构。
 */
class DataManager : public QObject
{
    Q_OBJECT

public:
    explicit DataManager(QObject *parent = nullptr);

public slots:
    /**
     * @brief [槽] 开始加载 CSV 文件
     * @param filePath 文件的完整路径
     */
    void loadCsvFile(const QString &filePath);

    /**
     * @brief [新增][槽] 开始加载 MAT 文件
     * @param filePath 文件的完整路径
     */
    void loadMatFile(const QString &filePath);

signals:
    /**
     * @brief [信号] 报告加载进度
     * @param percentage 0-100 的整数
     */
    void loadProgress(int percentage);

    /**
     * @brief [修改][信号] 数据加载成功完成
     * @param data 加载并解析后的数据 (包含 filePath 和一个或多个表)
     */
    void loadFinished(const FileData &data);

    /**
     * @brief [修改][信号] 数据加载失败
     * @param filePath 加载失败的文件
     * @param errorString 错误信息
     */
    void loadFailed(const QString &filePath, const QString &errorString);
};

#endif // DATAMANAGER_H