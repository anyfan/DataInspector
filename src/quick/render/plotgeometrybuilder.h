#pragma once

#include "plotlodbuilder.h"

struct PlotViewTransform
{
    double xMinimum = 0.0;
    double xMaximum = 1.0;
    double yMinimum = -1.0;
    double yMaximum = 1.0;
    double width = 0.0;
    double height = 0.0;

    bool isValid() const;
    QPointF map(const QPointF &point) const;
};

struct GeometryRequest
{
    PlotViewTransform transform;
    double lineWidth = 1.0;
};

struct GeometrySegment
{
    PlotSeriesId seriesId = -1;
    QColor color;
    QVector<QPointF> vertices;
    bool triangleList = false;
};

struct GeometryResult
{
    QVector<GeometrySegment> segments;
};

class PlotGeometryBuilder final
{
public:
    static GeometryResult build(const LodResult &lod,
                                const GeometryRequest &request);
};
