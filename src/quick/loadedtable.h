#pragma once

#include <QMetaType>
#include "render/plotrangeindex.h"
#include <QStringList>
#include <QVector>

struct LoadedTable
{
    QString name;
    QStringList signalNames;
    QVector<double> time;
    QVector<QVector<double>> values;
    QVector<bool> monotonicTimes;
    QVector<PlotRangeIndexPtr> rangeIndexes;
    qsizetype rowCount = 0;
    bool hasTimeBounds = false;
    double timeMinimum = 0.0;
    double timeMaximum = 0.0;
};

Q_DECLARE_METATYPE(LoadedTable)
Q_DECLARE_METATYPE(QVector<LoadedTable>)
