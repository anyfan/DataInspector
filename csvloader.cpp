#include "csvloader.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>

bool CsvLoader::loadCsv(const QString &filePath,
                      QVector<double> &keys,
                      QVector<QVector<double>> &values,
                      QStringList &headers)
{
    // 清空输出参数
    keys.clear();
    values.clear();
    headers.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Could not open file:" << filePath;
        return false;
    }

    QTextStream stream(&file);

    // 1. 读取 Header
    if (!stream.atEnd()) {
        QString headerLine = stream.readLine();
        headers = headerLine.split(',');
    }

    if (headers.isEmpty() || headers.count() < 2) {
        qWarning() << "CSV Error: No headers found or only one column.";
        file.close();
        return false; // 至少需要 1 个 key 列和 1 个 value 列
    }

    int numColumns = headers.count();
    int numValueColumns = numColumns - 1;

    // 2. 初始化 Value 向量
    values.resize(numValueColumns);

    // 3. 逐行读取数据
    int lineCount = 1; // (header 已经是第1行)
    while (!stream.atEnd()) {
        lineCount++;
        QString line = stream.readLine();
        if (line.trimmed().isEmpty()) {
            continue; // 跳过空行
        }

        QStringList parts = line.split(',');

        if (parts.count() != numColumns) {
            qWarning() << "Skipping malformed line" << lineCount << ": expected" << numColumns << "columns, got" << parts.count();
            continue; // 跳过格式错误的行
        }

        bool keyOk = false;
        
        // 4. 解析 Key (第1列)
        double key = parts[0].toDouble(&keyOk);
        if (!keyOk) {
            qWarning() << "Skipping line" << lineCount << ": Key is not a valid double.";
            continue;
        }
        keys.append(key);

        // 5. 解析 Values (第2列及以后)
        for (int i = 0; i < numValueColumns; ++i) {
            bool valueOk = false;
            double value = parts[i + 1].toDouble(&valueOk);
            if (valueOk) {
                values[i].append(value);
            } else {
                // 如果值无效，添加 NaN 或 0.0，以保持数据对齐
                qWarning() << "Invalid value at line" << lineCount << ", col" << (i+1);
                values[i].append(qQNaN()); // 使用 NaN (Not-a-Number)
            }
        }
    }

    file.close();

    // 确保所有向量大小一致
    for(int i = 0; i < numValueColumns; ++i) {
        if(values[i].size() != keys.size()) {
             qWarning() << "Data mismatch, keys and values vectors have different sizes.";
             // 这是一个更严重的数据损坏问题，但我们选择截断
             while(values[i].size() > keys.size()) values[i].removeLast();
             while(values[i].size() < keys.size()) values[i].append(qQNaN());
        }
    }

    qDebug() << "CSV Loaded. Headers:" << headers;
    qDebug() << "Loaded" << keys.count() << "data points for" << numValueColumns << "value columns.";

    return !keys.isEmpty();
}