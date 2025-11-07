#ifndef CSVLOADER_H
#define CSVLOADER_H

#include <QString>
#include <QVector>
#include <QStringList>

/**
 * @brief 简单的 CSV 加载工具类
 *
 * 提供一个静态方法来解析 CSV 文件。
 * 假设：
 * 1. 第一行是 Headers。
 * 2. 第一列是 Key (例如 Time)，类型为 double。
 * 3. 所有其他列是 Values，类型为 double。
 * 4. 分隔符是逗号 (,)。
 */
class CsvLoader
{
public:
    /**
     * @brief 从指定路径加载 CSV 文件
     * @param filePath CSV 文件的完整路径
     * @param keys (out) 存储第一列（Key/Time）数据
     * @param values (out) 存储所有其他列（Value）数据，每个内部 QVector 对应一列
     * @param headers (out) 存储第一行的列标题
     * @return true 如果加载和解析成功, 否则 false
     */
    static bool loadCsv(const QString &filePath,
                        QVector<double> &keys,
                        QVector<QVector<double>> &values,
                        QStringList &headers);
};

#endif // CSVLOADER_H