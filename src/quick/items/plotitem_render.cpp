// Scene Graph node ownership, geometry upload and curve ordering.
#include "plotitem.h"
#include <QMutexLocker>
#include <QtMath>
#include "render/plotgeometrybuilder.h"
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <algorithm>
#include <utility>

namespace {

// Persistent scene-graph state.  The render thread owns this object after it
// is returned from updatePaintNode(); keeping the nodes alive lets us resize
// and refill their vertex buffers instead of allocating a new node tree for
// every pan/zoom/cursor update.
struct PlotRoot final : QSGNode {
    QVector<QSGGeometryNode *> lineNodes;
    std::shared_ptr<const LodResult> curveResult;
    QVector<double> curveView;

    QSGGeometryNode *cursorNode = nullptr;
    QSGGeometryNode *cursorNode2 = nullptr;
};

static QSGGeometryNode *createLineNode(PlotRoot *root)
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    auto *material = new QSGFlatColorMaterial;
    node->setMaterial(material);
    node->setFlag(QSGNode::OwnsMaterial);
    if (root->cursorNode) root->insertChildNodeBefore(node, root->cursorNode);
    else root->appendChildNode(node);
    root->lineNodes.append(node);
    return node;
}

static void uploadLineNode(QSGGeometryNode *node,
                           const GeometrySegment &segment)
{
    auto *geometry = node->geometry();
    geometry->setDrawingMode(segment.triangleList
                             ? QSGGeometry::DrawTriangles
                             : QSGGeometry::DrawTriangleStrip);
    geometry->allocate(segment.vertices.size());
    auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
    for (int i = 0; i < segment.vertices.size(); ++i) {
        const QPointF point = segment.vertices.at(i);
        vertices[i].set(static_cast<float>(point.x()),
                        static_cast<float>(point.y()));
    }
    geometry->markVertexDataDirty();
    if (auto *material = static_cast<QSGFlatColorMaterial *>(node->material())) {
        material->setColor(segment.color);
    }
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}

} // namespace

QSGNode *PlotItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QMutexLocker lock(&m_dataMutex);
    auto *root = oldNode ? static_cast<PlotRoot *>(oldNode) : new PlotRoot;

    const double xs = qMax(m_xMaximum - m_xMinimum, 1e-12);
    const QVector<double> curveView{m_xMinimum, m_xMaximum, m_yMinimum, m_yMaximum,
                                    width(), height(), m_lineWidth, double(m_highlightedSeries),
                                    m_normalizeY ? 1.0 : 0.0, double(m_blendMode)};
    if (root->curveResult != m_lodResult || root->curveView != curveView) {
        const LodResult empty;
        LodResult lod = m_lodResult ? *m_lodResult : empty;
        if (m_normalizeY) {
            // Rescale the local render copy; the shared LOD stays raw.
            for (LodSegment &segment : lod.segments) {
                const auto it = m_normalization.constFind(segment.seriesId);
                if (it == m_normalization.cend()) continue;
                for (QPointF &point : segment.points)
                    if (qIsFinite(point.y()))
                        point.setY(it->second > 0.0 ? (point.y() - it->first) / it->second : 0.5);
            }
        }
        if (m_blendMode == AmplitudeLayers) {
            QHash<PlotSeriesId, double> amplitudes;
            for (const auto &series : m_seriesSnapshot.series) {
                const auto index = series->rangeIndex ? series->rangeIndex : PlotRangeIndex::build(*series);
                double meanAbsolute = 0.0;
                index->bounds(*series, m_xMinimum, m_xMaximum, nullptr, &meanAbsolute);
                amplitudes.insert(series->id, meanAbsolute);
            }
            // Large mean absolute amplitudes form the background. Equal amplitudes
            // retain their existing order, so panning does not arbitrarily swap them.
            std::stable_sort(lod.segments.begin(), lod.segments.end(),
                             [&amplitudes](const LodSegment &a, const LodSegment &b) {
                return amplitudes.value(a.seriesId) > amplitudes.value(b.seriesId);
            });
        }
        if (m_highlightedSeries >= 0) {
            // Emphasize in geometry only: no data copy or new LOD job.
            QVector<LodSegment> selected;
            QVector<LodSegment> remaining;
            for (auto segment : std::as_const(lod.segments)) {
                if (segment.seriesId == m_highlightedSeries) {
                    segment.lineWidth = (segment.lineWidth > 0 ? segment.lineWidth : m_lineWidth) + 2;
                    selected.append(std::move(segment));
                } else remaining.append(std::move(segment));
            }
            remaining.append(selected);
            lod.segments = std::move(remaining);
        }
        const GeometryRequest geometryRequest{
            {m_xMinimum, m_xMaximum, m_yMinimum, m_yMaximum, width(), height()},
            m_lineWidth};
        GeometryResult geometry = PlotGeometryBuilder::build(
            lod, geometryRequest);

        while (root->lineNodes.size() < geometry.segments.size())
            createLineNode(root);
        for (int i = 0; i < root->lineNodes.size(); ++i) {
            auto *node = root->lineNodes.at(i);
            if (i < geometry.segments.size()) {
                uploadLineNode(node, geometry.segments.at(i));
            } else if (node->geometry()) {
                node->geometry()->allocate(0);
                node->geometry()->markVertexDataDirty();
                node->markDirty(QSGNode::DirtyGeometry);
            }
        }
        root->curveResult = m_lodResult;
        root->curveView = curveView;
    }
    if (m_cursorMode != NoCursor && m_xMaximum > m_xMinimum) {
        const double x = (m_cursorX1 - m_xMinimum) / xs * width();
        if (!root->cursorNode) {
            root->cursorNode = new QSGGeometryNode;
            auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
            geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            root->cursorNode->setGeometry(geometry);
            root->cursorNode->setFlag(QSGNode::OwnsGeometry);
            auto *material = new QSGFlatColorMaterial;
            material->setColor(QColor("#e34d59"));
            root->cursorNode->setMaterial(material);
            root->cursorNode->setFlag(QSGNode::OwnsMaterial);
            root->appendChildNode(root->cursorNode);
        }
        auto *geometry = root->cursorNode->geometry();
        geometry->allocate(4);
        auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
        vertices[0].set(x - 0.75, 0); vertices[1].set(x + 0.75, 0);
        vertices[2].set(x - 0.75, height()); vertices[3].set(x + 0.75, height());
        geometry->markVertexDataDirty();
        root->cursorNode->markDirty(QSGNode::DirtyGeometry);
        if (m_cursorMode == DoubleCursor) {
            if (!root->cursorNode2) {
                auto *second = new QSGGeometryNode;
                auto *g2 = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
                g2->setDrawingMode(QSGGeometry::DrawTriangleStrip);
                second->setGeometry(g2);
                second->setFlag(QSGNode::OwnsGeometry);
                auto *m2 = new QSGFlatColorMaterial;
                m2->setColor(QColor("#4e79e7"));
                second->setMaterial(m2);
                second->setFlag(QSGNode::OwnsMaterial);
                root->appendChildNode(second);
                root->cursorNode2 = second;
            }
            auto *second = root->cursorNode2;
            const double x2 = (m_cursorX2 - m_xMinimum) / xs * width();
            auto *g2 = second->geometry();
            g2->allocate(4);
            auto *v2 = static_cast<QSGGeometry::Point2D *>(g2->vertexData());
            v2[0].set(x2 - .75, 0);
            v2[1].set(x2 + .75, 0);
            v2[2].set(x2 - .75, height());
            v2[3].set(x2 + .75, height());
            g2->markVertexDataDirty();
            second->markDirty(QSGNode::DirtyGeometry);
        } else if (root->cursorNode2 && root->cursorNode2->geometry()) {
            root->cursorNode2->geometry()->allocate(0);
            root->cursorNode2->geometry()->markVertexDataDirty();
            root->cursorNode2->markDirty(QSGNode::DirtyGeometry);
        }
    } else if (root->cursorNode && root->cursorNode->geometry()) {
        root->cursorNode->geometry()->allocate(0);
        root->cursorNode->geometry()->markVertexDataDirty();
        root->cursorNode->markDirty(QSGNode::DirtyGeometry);
        if (root->cursorNode2 && root->cursorNode2->geometry()) {
            root->cursorNode2->geometry()->allocate(0);
            root->cursorNode2->geometry()->markVertexDataDirty();
            root->cursorNode2->markDirty(QSGNode::DirtyGeometry);
        }
    }
    return root;
}
