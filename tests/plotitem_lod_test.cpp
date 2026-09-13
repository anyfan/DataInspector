#include "plotitem.h"
#include "render/plotseriesstore.h"

#include <QGuiApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QSGGeometryNode>

#include <iostream>

class TestPlotItem final : public PlotItem
{
public:
    using PlotItem::PlotItem;

    QSGNode *paint(QSGNode *oldNode = nullptr)
    {
        QElapsedTimer deadline;
        deadline.start();
        while (lodPending() && deadline.elapsed() < 10000) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        if (lodPending()) qFatal("LOD did not finish");
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
    if (vertexCount != 4) {
        std::cerr << "Expected a two-point line crossing the viewport, got "
                  << vertexCount << " vertices\n";
        return 1;
    }
    const auto *vertices = static_cast<const QSGGeometry::Point2D *>(
        lineNode->geometry()->vertexData());
    for (int i = 0; i < lineNode->geometry()->vertexCount(); ++i) {
        if (!qIsFinite(vertices[i].x) || !qIsFinite(vertices[i].y)
            || vertices[i].x < 0.0f || vertices[i].x > plot.width()
            || vertices[i].y < 0.0f || vertices[i].y > plot.height()) {
            std::cerr << "Scene graph received an invalid or unclipped vertex\n";
            delete root;
            return 1;
        }
    }

    plot.setLineWidth(6.0);
    root = plot.paint(root);
    lineNode = dynamic_cast<QSGGeometryNode *>(root->firstChild());
    if (!lineNode || !lineNode->geometry()
        || lineNode->geometry()->vertexCount() != 4) {
        std::cerr << "Line geometry disappeared after a visual-only update\n";
        delete root;
        return 1;
    }
    // A cursor-only paint must leave existing curve vertex data untouched.
    auto *savedVertices = static_cast<QSGGeometry::Point2D *>(lineNode->geometry()->vertexData());
    savedVertices[0].x = 123.0f;
    plot.setCursorMode(PlotItem::SingleCursor);
    plot.setCursorPosition(0.5);
    root = plot.paint(root);
    if (savedVertices[0].x != 123.0f) {
        std::cerr << "Cursor update unnecessarily uploaded curve geometry\n";
        delete root;
        return 1;
    }
    delete root;

    QVector<PlotSeriesInput> inputs;
    QVector<PlotSeriesId> ids;
    QVector<double> time(100000), values(100000);
    for (int i = 0; i < time.size(); ++i) { time[i] = i; values[i] = i % 101; }
    for (int i = 0; i < 16; ++i) {
        inputs.append({i, time, values, QColor("red")});
        ids.append(i);
    }
    store->replaceSeries(inputs);
    plot.setVisibleSeries(ids);
    plot.fitView();
    root = plot.paint();
    auto *last = dynamic_cast<QSGGeometryNode *>(root->lastChild());
    if (!last || last->geometry()->vertexCount() != 4) {
        std::cerr << "Cursor must be the top scene graph node\n";
        delete root;
        return 1;
    }
    // Adding more curves while the cursor exists must insert them below it.
    inputs.append({16, time, values, QColor("blue")});
    ids.append(16);
    store->replaceSeries(inputs);
    plot.setVisibleSeries(ids);
    root = plot.paint(root);
    if (root->lastChild() != last) {
        std::cerr << "New curve covered the cursor\n";
        delete root;
        return 1;
    }
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 200; ++i) {
        plot.setCursorX(20000 + i * 100);
        root = plot.paint(root);
    }
    std::cout << "17 signals x 100000 samples, 200 cursor updates: "
              << timer.elapsed() << " ms (CPU query and scene graph submission)\n";
    delete root;
    return 0;
}
