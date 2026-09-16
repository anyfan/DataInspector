#pragma once

#include "loadedtable.h"

#include <QString>
#include <QVector>

#include <functional>

struct XlsxReadResult
{
    QVector<LoadedTable> tables;
    int skippedRows = 0;
    QString error;
    bool cancelled = false;
};

XlsxReadResult readXlsxWorkbook(
    const QString &path, const std::function<void(int)> &reportProgress,
    const std::function<bool()> &isCancelled);
