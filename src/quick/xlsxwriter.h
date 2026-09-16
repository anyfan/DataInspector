#pragma once

#include "render/plotseriesstore.h"

#include <QString>
#include <QVector>

#include <functional>

struct XlsxExportSeries
{
    QString name;
    PlotSeriesDataPtr data;
};

struct XlsxExportTable
{
    QString name;
    QVector<XlsxExportSeries> series;
};

struct XlsxWriteOptions
{
    qsizetype maxDataRowsPerSheet = 1048575;
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
