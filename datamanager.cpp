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
 * @brief [辅助函数] 处理一组 MAT 变量 (pX, pX_title, pX_title2) 并构建 SignalTable
 */
static bool processMatTable(int index,
                            const QMap<QString, matvar_t *> &varMap,
                            SignalTable &table)
{
    QString pName = QString("p%1").arg(index);
    QString pTitleName = QString("p%1_title").arg(index);
    QString pTitle2Name = QString("p%1_title2").arg(index);

    matvar_t *pVar = varMap.value(pName);
    if (!pVar)
        return false;

    // 验证数据有效性
    if (pVar->data_type != MAT_T_DOUBLE || pVar->rank != 2 || pVar->dims[0] == 0 || pVar->dims[1] < 2)
    {
        qWarning() << "DataManager: Skipping variable" << pName << "- invalid format.";
        return false;
    }

    table.name = pName;

    // 读取标题
    matvar_t *titleVar = varMap.value(pTitleName, NULL);
    matvar_t *title2Var = varMap.value(pTitle2Name, NULL);

    QStringList titleList1 = readMatStringArray(titleVar);
    QStringList titleList2 = readMatStringArray(title2Var);

    size_t rows = pVar->dims[0];
    size_t cols = pVar->dims[1];
    int numValueCols = cols - 1;
    double *data = static_cast<double *>(pVar->data);

    // 处理 Headers 逻辑
    if (titleList1.size() == numValueCols + 1)
        titleList1.removeFirst();
    if (titleList2.size() == numValueCols + 1)
        titleList2.removeFirst();

    if (titleList1.size() == numValueCols && titleList2.size() == numValueCols)
    {
        for (int j = 0; j < numValueCols; ++j)
            table.headers.append(titleList2.at(j) + " " + titleList1.at(j));
    }
    else if (titleList2.size() == numValueCols)
    {
        table.headers = titleList2;
    }
    else if (titleList1.size() == numValueCols)
    {
        table.headers = titleList1;
    }
    else
    {
        for (int j = 0; j < numValueCols; ++j)
            table.headers.append(QString("%1_Sig%2").arg(pName).arg(j + 1));
    }

    // 拷贝数据
    table.valueData.resize(numValueCols);
    if (rows > 0)
    {
        table.timeData.resize(rows);
        std::copy(data, data + rows, table.timeData.begin()); // 第一列是时间

        for (size_t c = 1; c < cols; ++c)
        {
            table.valueData[c - 1].resize(rows);
            const double *colPtr = data + (c * rows);
            std::copy(colPtr, colPtr + rows, table.valueData[c - 1].begin());
        }
    }
    return true;
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

    QByteArray cFilePath = filePath.toUtf8();
    mat_t *matfile = Mat_Open(cFilePath.constData(), MAT_ACC_RDONLY);

    if (matfile == NULL)
    {
        emit loadFailed(filePath, tr("Error opening .mat file: %1").arg(filePath));
        return;
    }

    // 1. 扫描所有变量
    QMap<QString, matvar_t *> varMap;
    matvar_t *variable = NULL;
    QRegularExpression keepRegex("^p\\d+(_title2?)?$");
    QRegularExpression pVarRegex("^p(\\d+)$");
    QList<int> pIndices;

    while ((variable = Mat_VarReadNext(matfile)) != NULL)
    {
        QString name = QString::fromLatin1(variable->name);
        if (keepRegex.match(name).hasMatch())
        {
            varMap.insert(name, variable);
            // 顺便收集索引
            QRegularExpressionMatch match = pVarRegex.match(name);
            if (match.hasMatch())
            {
                bool ok;
                int idx = match.captured(1).toInt(&ok);
                if (ok && idx > 0)
                    pIndices.append(idx);
            }
        }
        else
        {
            Mat_VarFree(variable);
        }
    }
    emit loadProgress(10);

    // 2. 排序索引
    std::sort(pIndices.begin(), pIndices.end());

    // 3. 处理每个 p 变量
    for (int loop_idx = 0; loop_idx < pIndices.size(); ++loop_idx)
    {
        int i = pIndices.at(loop_idx);
        SignalTable table;

        // 调用我们提取的辅助函数
        if (processMatTable(i, varMap, table))
        {
            fileData.tables.append(table);
        }

        // 更新进度
        if (pIndices.size() > 0)
            emit loadProgress(10 + 80 * (loop_idx + 1) / pIndices.size());
    }

    // 4. 清理
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