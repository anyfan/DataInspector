#include "datamanager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

#include <stdio.h>
#include <string.h>
#include "matio.h"
#include <stdlib.h>
#include <QMap>

/**
 * @brief [辅助函数] 返回一个 UTF-8 字符的字节长度
 * @param c 字符串的第一个字节
 * @return 1, 2, 3, 或 4，即该字符占用的字节数
 */
static int utf8_char_len(unsigned char c)
{
    if (c < 0x80)
        return 1; // 0xxxxxxx (ASCII)
    else if ((c & 0xE0) == 0xC0)
        return 2; // 110xxxxx
    else if ((c & 0xF0) == 0xE0)
        return 3; // 1110xxxx
    else if ((c & 0xF8) == 0xF0)
        return 4; // 11110xxx
    return 1;     // 无效或损坏，当作1字节处理
}

/**
 * @brief [辅助函数] 从 matvar_t (MAT_T_UTF8 或 MAT_T_CHAR) 中读取字符串数组
 * * 基于您的示例代码。
 * @param variable 指向 matio 变量的指针
 * @return 字符串列表
 */
static QStringList readMatStringArray(matvar_t *variable)
{
    QStringList result;
    if (variable == NULL || variable->data == NULL || variable->rank != 2)
        return result;

    size_t rows = variable->dims[0];
    size_t cols = variable->dims[1];
    if (rows == 0 || cols == 0)
        return result;

    if (variable->data_type == MAT_T_UTF8)
    {
        QVector<QByteArray> buffers(rows);

        unsigned char *data_scanner = (unsigned char *)variable->data;

        // MATLAB 是列优先存储，所以外层循环是 cols，内层是 rows
        for (size_t j = 0; j < cols; j++)
        {
            for (size_t i = 0; i < rows; i++)
            {
                int len = utf8_char_len(*data_scanner);
                buffers[i].append((const char *)data_scanner, len);
                data_scanner += len;
            }
        }

        // 转换结果
        result.reserve(rows);
        for (const QByteArray &buf : buffers)
        {
            result.append(QString::fromUtf8(buf).trimmed());
        }
        return result;
    }
    else if (variable->data_type == MAT_T_INT8 || variable->data_type == MAT_T_UINT8)
    {
        const char *data = (const char *)variable->data;
        result.reserve(rows);

        for (size_t i = 0; i < rows; i++)
        {
            QByteArray row_data;
            row_data.reserve(cols);
            // 列优先读取：第 i 行的第 j 个字符在 index = j * rows + i
            for (size_t j = 0; j < cols; j++)
            {
                char c = data[j * rows + i];
                if (c == '\0')
                    break;
                row_data.append(c);
            }
            result.append(QString::fromLocal8Bit(row_data).trimmed());
        }
        return result;
    }

    return result;
}

/**
 * @brief [辅助函数] 从 matvar_t (MAT_T_UTF8 或 MAT_T_CHAR) 中读取单个字符串
 * * 假设它是一个 [1xN] 或 [Nx1] 的 char 数组
 */
static QString readMatString(matvar_t *variable)
{
    //  移除类型检查，让 readMatStringArray 处理
    if (variable == NULL || variable->data == NULL || variable->rank != 2)
    {
        return QString();
    }
    QStringList list = readMatStringArray(variable);
    if (!list.isEmpty())
    {
        return list.first();
    }
    return QString();
}

// 注册 FileData 类型，以便在信号槽中使用
DataManager::DataManager(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<FileData>("FileData");
}

void DataManager::loadCsvFile(const QString &filePath)
{
    FileData fileData;
    fileData.filePath = filePath;

    SignalTable table;                                   // CSV 文件只有一个表
    table.name = QFileInfo(filePath).completeBaseName(); // 使用文件名作为表名

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        emit loadFailed(filePath, tr("Could not open file: %1").arg(filePath));
        return;
    }

    qint64 fileSize = file.size();
    QTextStream stream(&file);

    // 1. 读取 Header
    if (!stream.atEnd())
    {
        QString headerLine = stream.readLine();
        table.headers = headerLine.split(',');
    }

    if (table.headers.isEmpty() || table.headers.count() < 2)
    {
        emit loadFailed(filePath, tr("CSV Error: No headers or only one column."));
        file.close();
        return;
    }

    int numColumns = table.headers.count();
    int numValueColumns = numColumns - 1;

    // 移除第一个头 (时间列)
    table.headers.removeFirst();

    // 2. 初始化 Value 向量
    table.valueData.resize(numValueColumns);

    if (fileSize > 0)
    {
        qint64 estimatedRows = fileSize / 50;
        table.timeData.reserve(estimatedRows);
        for (int i = 0; i < numValueColumns; ++i)
        {
            table.valueData[i].reserve(estimatedRows);
        }
    }

    // 3. 逐行读取数据
    int lineCount = 1;
    qint64 lastReportedProgress = 0;

    while (!stream.atEnd())
    {
        lineCount++;
        QString line = stream.readLine();
        if (line.trimmed().isEmpty())
        {
            continue;
        }

        // 报告进度
        qint64 currentPos = file.pos();
        int percentage = (fileSize > 0) ? (static_cast<double>(currentPos) / fileSize * 100) : 0;
        if (percentage > lastReportedProgress)
        {
            emit loadProgress(percentage);
            lastReportedProgress = percentage;
        }

        QStringList parts = line.split(',');
        if (parts.count() != numColumns)
        {
            qWarning() << "Skipping malformed line" << lineCount;
            continue;
        }

        bool keyOk = false;
        double key = parts[0].toDouble(&keyOk);
        if (!keyOk)
        {
            qWarning() << "Skipping line" << lineCount << ": Key is not a valid double.";
            continue;
        }
        table.timeData.append(key);

        for (int i = 0; i < numValueColumns; ++i)
        {
            bool valueOk = false;
            double value = parts[i + 1].toDouble(&valueOk);
            if (valueOk)
            {
                table.valueData[i].append(value);
            }
            else
            {
                table.valueData[i].append(qQNaN());
            }
        }
    }
    file.close();

    // 确保数据对齐 (不应需要，但作为安全检查)
    for (int i = 0; i < numValueColumns; ++i)
    {
        while (table.valueData[i].size() < table.timeData.size())
        {
            table.valueData[i].append(qQNaN());
        }
    }

    fileData.tables.append(table);
    emit loadProgress(100);
    emit loadFinished(fileData);
    qDebug() << "DataManager: CSV Load finished on thread" << QThread::currentThreadId();
}

/**
 * @brief 加载 MAT 文件
 */
void DataManager::loadMatFile(const QString &filePath)
{
    FileData fileData;
    fileData.filePath = filePath;

    // MATIO 需要 const char*，所以我们转换路径
    QByteArray cFilePath = filePath.toUtf8();
    mat_t *matfile = Mat_Open(cFilePath.constData(), MAT_ACC_RDONLY);

    if (matfile == NULL)
    {
        emit loadFailed(filePath, tr("Error opening .mat file: %1").arg(filePath));
        return;
    }

    // 1. 将所有变量读入 QMap 以便快速查找
    QMap<QString, matvar_t *> varMap;
    matvar_t *variable = NULL;
    while ((variable = Mat_VarReadNext(matfile)) != NULL)
    {
        varMap.insert(QString(variable->name), variable);
    }
    emit loadProgress(10); // 10% for reading directory

    // 2. 遍历所有找到的变量，查找 "p" + 数字 格式的变量
    QRegularExpression pVarRegex("^p(\\d+)$");
    QList<int> pIndices;
    for (const QString &key : varMap.keys())
    {
        QRegularExpressionMatch match = pVarRegex.match(key);
        if (match.hasMatch())
        {
            bool ok;
            int index = match.captured(1).toInt(&ok);
            if (ok && index > 0)
            {
                pIndices.append(index);
            }
        }
    }

    // 对索引进行数字排序 (例如: p1, p2, p5, p998, p999)
    std::sort(pIndices.begin(), pIndices.end());

    // 现在使用排序后的索引列表进行循环
    for (int loop_idx = 0; loop_idx < pIndices.size(); ++loop_idx)
    {
        int i = pIndices.at(loop_idx); // 获取 p 后面的数字 (例如 1, 2, 5, 998, 999)

        QString pName = QString("p%1").arg(i);
        QString pTitleName = QString("p%1_title").arg(i);
        QString pTitle2Name = QString("p%1_title2").arg(i);

        // 我们已经从 varMap 中确认了 pName 存在 (在上面的 regex 循环中)
        matvar_t *pVar = varMap.value(pName);

        if (pVar->data_type != MAT_T_DOUBLE || pVar->rank != 2 || pVar->dims[0] == 0 || pVar->dims[1] < 2)
        {
            qWarning() << "DataManager: Skipping variable" << pName << "- not a 2D double matrix or not enough columns.";
            continue;
        }

        SignalTable table;

        // 3. 获取表名 (pX)
        table.name = pName;

        // 4. 获取 pX_title 和 pX_title2
        matvar_t *titleVar = varMap.value(pTitleName, NULL);
        matvar_t *title2Var = varMap.value(pTitle2Name, NULL);

        QStringList titleList1 = readMatStringArray(titleVar);
        QStringList titleList2 = readMatStringArray(title2Var);

        // 5. 读取数值数据 (提前获取列数)
        size_t rows = pVar->dims[0]; // N 个数据点
        size_t cols = pVar->dims[1]; // M 个通道 (1 个时间 + M-1 个信号)
        int numValueCols = cols - 1;
        double *data = static_cast<double *>(pVar->data);

        // 6.  验证/调整/组合 headers

        // 检查 titleList1 和 titleList2 是否可能包含时间列
        if (titleList1.size() == numValueCols + 1)
        {
            titleList1.removeFirst(); // 移除 "时间"
        }
        if (titleList2.size() == numValueCols + 1)
        {
            titleList2.removeFirst(); // 移除 "时间"
        }

        // 根据用户逻辑组合: signal_name[n] = pX_title[n] + pX_title2[n]
        if (titleList1.size() == numValueCols && titleList2.size() == numValueCols)
        {
            // 组合成功
            for (int j = 0; j < numValueCols; ++j)
            {
                // 用户的公式是 `pX_title[n] + pX_title2[n]`，我们按字面意思执行。
                table.headers.append(titleList2.at(j) + " " + titleList1.at(j));
            }
        }
        //  [回退逻辑]
        else if (titleList2.size() == numValueCols)
        {
            // pX_title 列表无效，但 pX_title2 有效。
            qWarning() << "DataManager: Mismatch for" << pTitleName << "(size" << titleList1.size() << "). Falling back to" << pTitle2Name << "only.";
            table.headers = titleList2;
        }
        else if (titleList1.size() == numValueCols)
        {
            // pX_title2 列表无效，但 pX_title 有效。
            qWarning() << "DataManager: Mismatch for" << pTitle2Name << "(size" << titleList2.size() << "). Falling back to" << pTitleName << "only.";
            table.headers = titleList1;
        }
        //  [默认值]
        else // 两个列表都无效，生成默认值
        {
            qWarning() << "DataManager: Header count mismatch for" << pTitleName << "(size" << titleList1.size() << ") and" << pTitle2Name << "(size" << titleList2.size() << "). Expected" << numValueCols << ". Generating defaults.";
            table.headers.clear();
            for (int j = 0; j < numValueCols; ++j)
            {
                table.headers.append(QString("%1_Sig%2").arg(pName).arg(j + 1));
            }
        }

        // 7. 调整值数据的大小
        table.valueData.resize(numValueCols);
        table.timeData.reserve(rows);
        for (int j = 0; j < numValueCols; ++j)
        {
            table.valueData[j].reserve(rows);
        }

        // MATLAB/Matio 以列优先顺序存储数据 data[ (col * rows) + row ]
        for (size_t r = 0; r < rows; ++r)
        {
            // Col 0 是时间
            table.timeData.append(data[r]); // data[ (0 * rows) + r ]

            // Cols 1...M 是信号
            for (size_t c = 1; c < cols; ++c)
            {
                table.valueData[c - 1].append(data[(c * rows) + r]);
            }
        }

        // 8. 将此表添加到 FileData
        fileData.tables.append(table);

        // (更新进度)
        if (pIndices.size() > 0)
        {
            emit loadProgress(10 + 80 * (loop_idx + 1) / pIndices.size());
        }
    }

    // 9. 清理 matvar_t*
    qDeleteAll(varMap);
    varMap.clear();

    Mat_Close(matfile);

    if (fileData.tables.isEmpty())
    {
        emit loadFailed(filePath, tr("MAT file contains no valid 'p' variables."));
        return;
    }

    emit loadProgress(100);
    emit loadFinished(fileData);
    qDebug() << "DataManager: MAT Load finished on thread" << QThread::currentThreadId();
}