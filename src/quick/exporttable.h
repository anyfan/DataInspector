#pragma once

#include "render/plotseriesstore.h"

#include <QString>
#include <QVector>

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
