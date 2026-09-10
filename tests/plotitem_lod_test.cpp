#include "plotitem.h"
#include "render/plotseriesstore.h"

#include <QGuiApplication>
#include <QSGGeometryNode>

#include <iostream>

class TestPlotItem final : public PlotItem
{
public:
    using PlotItem::PlotItem;

    QSGNode *paint(QSGNode *oldNode = nullptr)
    {
        return updatePaintNode(oldNode, nullptr);
    }
};

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    TestPlotItem plot;
    plot.setWidth(800);
    plot.setHeight(400);
    auto store = std::make_shared<PlotSeriesStore>();
    store->replaceSeries({{7, {0.0, 1.0}, {0.0, 1.0}, QColor("#4ea1ff")}});
    plot.setSeriesStore(store);
    plot.setVisibleSeries({7});
    plot.setXRange(0.4, 0.6);

    QSGNode *root = plot.paint();
    auto *lineNode = dynamic_cast<QSGGeometryNode *>(root->firstChild());
    const int vertexCount = lineNode && lineNode->geometry()
            ? lineNode->geometry()->vertexCount() : 0;
    delete root;

    if (vertexCount != 4) {
        std::cerr << "Expected a two-point line crossing the viewport, got "
                  << vertexCount << " vertices\n";
        return 1;
    }
    return 0;
}
