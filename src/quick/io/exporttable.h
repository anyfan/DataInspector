#pragma once

#include "render/plotseriesstore.h"

#include <QString>
#include <QVector>
#include <functional>

// One exported column: the display name plus the shared immutable samples.
struct DataExportSeries
{
    QString name;
    PlotSeriesDataPtr data;
};

// One exported table (an Excel worksheet or a MAT pN matrix). Rows are
// indexed against the first series, whose time base becomes the Time column.
struct DataExportTable
{
    QString name;
    QVector<DataExportSeries> series;
};

// Runs on the export worker before touching the destination file.
struct ExportValidationResult
{
    QString error;
    bool cancelled = false;
};
ExportValidationResult validateExportTimeBases(
    const QVector<DataExportTable> &tables,
    const std::function<bool()> &isCancelled = {});
