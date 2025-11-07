#ifndef DATAMANAGER_H
#define DATAMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QStringList>

// 这是一个简单的数据容器，用于通过信号槽传递
// 我们使用 Q_DECLARE_METATYPE 使其能用于排队的信号槽
struct CsvData
{
    QStringList headers;
    QVector<double> timeData;
    QVector<QVector<double>> valueData;
};
Q_DECLARE_METATYPE(CsvData)

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

signals:
    /**
     * @brief [信号] 报告加载进度
     * @param percentage 0-100 的整数
     */
    void loadProgress(int percentage);

    /**
     * @brief [信号] 数据加载成功完成
     * @param data 加载并解析后的数据
     */
    void loadFinished(const CsvData &data);

    /**
     * @brief [信号] 数据加载失败
     * @param errorString 错误信息
     */
    void loadFailed(const QString &errorString);
};

#endif // DATAMANAGER_H