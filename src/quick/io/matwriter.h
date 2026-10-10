#pragma once

#include "exporttable.h"

#include <QString>
#include <QVector>

#include <functional>

struct MatWriteResult
{
    QString error;
    bool cancelled = false;
};

// True when this build links matio; otherwise writeMatFile() only reports an error.
bool matExportSupported();

// Writes a MATLAB Level 5 file laid out like the files the loader imports:
// every table becomes an uncompressed double matrix pN whose first column is
// time and the remaining columns are signals (NaN marks gaps), plus a UTF-8
// char matrix pN_title holding "Time" and the signal names, one per row.
// Tables that originate from a MAT table keep their pN number when it is free.
// The target file is only replaced after the whole export succeeded.
MatWriteResult writeMatFile(
    const QString &path, const QVector<DataExportTable> &tables,
    const std::function<void(int)> &reportProgress = {},
    const std::function<bool()> &isCancelled = {});
