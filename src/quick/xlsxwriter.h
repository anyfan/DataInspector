#pragma once

#include "exporttable.h"

#include <QString>
#include <QVector>

#include <functional>

using XlsxExportSeries = DataExportSeries;
using XlsxExportTable = DataExportTable;

struct XlsxWriteOptions
{
    qsizetype maxDataRowsPerSheet = 1048575;
    bool zipCompressionEnabled = false;
};

struct XlsxWriteResult
{
    QString error;
    bool cancelled = false;
};

XlsxWriteResult writeXlsxWorkbook(
    const QString &path, const QVector<XlsxExportTable> &tables,
    const XlsxWriteOptions &options = {},
    const std::function<void(int)> &reportProgress = {},
    const std::function<bool()> &isCancelled = {});
