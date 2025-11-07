#include "datamanager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QThread>

// 注册 CsvData 类型，以便在信号槽中使用
DataManager::DataManager(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<CsvData>("CsvData");
}

void DataManager::loadCsvFile(const QString &filePath)
{
    CsvData data;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        emit loadFailed(tr("Could not open file: %1").arg(filePath));
        return;
    }

    qint64 fileSize = file.size();
    QTextStream stream(&file);

    // 1. 读取 Header
    if (!stream.atEnd())
    {
        QString headerLine = stream.readLine();
        data.headers = headerLine.split(',');
    }

    if (data.headers.isEmpty() || data.headers.count() < 2)
    {
        emit loadFailed(tr("CSV Error: No headers or only one column."));
        file.close();
        return;
    }

    int numColumns = data.headers.count();
    int numValueColumns = numColumns - 1;

    // 2. 初始化 Value 向量
    data.valueData.resize(numValueColumns);

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

        // 避免过于频繁地发送信号
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
        data.timeData.append(key);

        for (int i = 0; i < numValueColumns; ++i)
        {
            bool valueOk = false;
            double value = parts[i + 1].toDouble(&valueOk);
            if (valueOk)
            {
                data.valueData[i].append(value);
            }
            else
            {
                data.valueData[i].append(qQNaN());
            }
        }
    }

    file.close();

    // 确保数据对齐
    for (int i = 0; i < numValueColumns; ++i)
    {
        while (data.valueData[i].size() < data.timeData.size())
        {
            data.valueData[i].append(qQNaN());
        }
    }

    emit loadProgress(100);
    emit loadFinished(data);
    qDebug() << "DataManager: Load finished on thread" << QThread::currentThreadId();
}