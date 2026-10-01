#pragma once
#include <QQuickItem>
#include <QColor>
#include <QVector>
#include <QMutex>
#include <QHash>
#include <QVariantList>
#include <memory>

#include "render/plotseriesstore.h"
#include "render/plotlodbuilder.h"
#include "render/plotlodscheduler.h"

// Shared state and locking stay in one item; implementation is separated into
// plotitem.cpp (views), _cursor.cpp, _interaction.cpp and _render.cpp.
class PlotItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int highlightedSeries READ highlightedSeries WRITE setHighlightedSeries NOTIFY highlightedSeriesChanged)
    Q_PROPERTY(bool lodPending READ lodPending NOTIFY lodChanged)
    Q_PROPERTY(int cursorMode READ cursorMode WRITE setCursorMode NOTIFY cursorChanged)
    Q_PROPERTY(double xMinimum READ xMinimum NOTIFY viewChanged)
    Q_PROPERTY(double xMaximum READ xMaximum NOTIFY viewChanged)
    Q_PROPERTY(double yMinimum READ yMinimum NOTIFY viewChanged)
    Q_PROPERTY(double yMaximum READ yMaximum NOTIFY viewChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(double cursorX1 READ cursorX1 NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX2 READ cursorX2 NOTIFY cursorChanged)
    Q_PROPERTY(double cursorDeltaT READ cursorDeltaT NOTIFY cursorDeltaTChanged)
    Q_PROPERTY(QVariantList xTicks READ xTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList yTicks READ yTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList cursorReadouts READ cursorReadouts NOTIFY cursorValuesChanged)
    // Draw every series scaled to [0, 1] by its own min/max. Cursor readouts
    // keep reporting raw values; only the drawn position is normalized.
    Q_PROPERTY(bool normalizeY READ normalizeY WRITE setNormalizeY NOTIFY normalizeYChanged)
    // Curve order: 0 binding order, 3 small amplitudes in front.
    // Keep value 3 for the existing QML enum; retired values 1/2 fall back to 0.
    Q_PROPERTY(int blendMode READ blendMode WRITE setBlendMode NOTIFY blendModeChanged)
public:
    enum CursorMode { NoCursor = 0, SingleCursor = 1, DoubleCursor = 2 };
    Q_ENUM(CursorMode)
    enum BlendMode { OpaqueBlend = 0, AmplitudeLayers = 3 };
    Q_ENUM(BlendMode)
    explicit PlotItem(QQuickItem *parent = nullptr);
    int highlightedSeries() const;
    void setHighlightedSeries(int id);
    bool lodPending() const { return m_lodScheduler->pending(); }
    double xMinimum() const;
    double xMaximum() const;
    double yMinimum() const;
    double yMaximum() const;
    double lineWidth() const;
    int cursorMode() const;
    double cursorX1() const;
    double cursorX2() const;
    double cursorDeltaT() const;
    QVariantList xTicks() const;
    QVariantList yTicks() const;
    QVariantList cursorReadouts() const;
    bool normalizeY() const;
    void setNormalizeY(bool enabled);
    int blendMode() const;
    void setBlendMode(int mode);
    void setLineWidth(double width);
    void setCursorMode(int mode);
    void setSeriesStore(const std::shared_ptr<const PlotSeriesStore> &store);
    void setVisibleSeries(const QVector<PlotSeriesId> &orderedIds);
    QVector<PlotSeriesId> visibleSeriesIds() const;
    Q_INVOKABLE void setCursorX(double x, int cursorIndex = 1);
    void setCursorPosition(double x, int cursorIndex = 1);
    // Session/peer state is exact, including disabled or off-screen cursors.
    void restoreCursorState(int mode, double x1, double x2);
    // direction < 0 moves to the previous raw sample, direction > 0 to the next;
    // cursorIndex 0 steps every cursor enabled by the current mode.
    Q_INVOKABLE void stepCursor(int direction, int cursorIndex = 0);
    Q_INVOKABLE void fitView();
    Q_INVOKABLE void zoomAxis(int axis, double fraction, double steps);
    Q_INVOKABLE void fitY();
    Q_INVOKABLE void setXRange(double xMinimum, double xMaximum);
    Q_INVOKABLE void setYRange(double yMinimum, double yMaximum);
    Q_INVOKABLE void selectSeriesAt(double pixelX, double pixelY);
signals:
    void normalizeYChanged();
    void blendModeChanged();
    void highlightedSeriesChanged();
    void lodChanged();
    void viewChanged();
    void lineWidthChanged();
    void cursorChanged();
    void cursorDeltaTChanged();
    void cursorValuesChanged();
    void axisTicksChanged();
    void activated();
    void seriesClicked(int seriesId);
    void rangeAboutToChange();
    void viewInteractionStarted();
    void viewInteractionFinished();
    void rangeChanged(double xMinimum, double xMaximum, double yMinimum, double yMaximum);
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
private:
    double nearestRawX(double x) const;
    void updateCursorValuesLocked();
    void rebuildTicksLocked();
    bool cursorHit(double pixelX, int *cursorIndex) const;
    void refreshSnapshotLocked();
    void rebuildNormalizationLocked();
    double normalizedYLocked(PlotSeriesId id, double y) const;
    void setRange(double xMinimum, double xMaximum, double yMinimum, double yMaximum);
    QPointF pixelToData(const QPointF &pixel) const;
    std::shared_ptr<const PlotSeriesStore> m_seriesStore;
    QVector<PlotSeriesId> m_visibleSeries;
    PlotSeriesSnapshot m_seriesSnapshot;
    bool m_normalizeY = false;
    int m_blendMode = OpaqueBlend;
    // Per-series (minimum, span) used when m_normalizeY is set.
    QHash<PlotSeriesId, QPair<double, double>> m_normalization;
    PlotLodScheduler *m_lodScheduler;
    std::shared_ptr<const LodResult> m_lodResult;
    void requestLod();
    double m_xMinimum = 0.0, m_xMaximum = 1.0, m_yMinimum = -1.0, m_yMaximum = 1.0;
    double m_lineWidth = 2.0;
    int m_highlightedSeries = -1;
    int m_cursorMode = NoCursor;
    double m_cursorX1 = 0.0;
    double m_cursorX2 = 1.0;
    QVariantList m_xTicks;
    QVariantList m_yTicks;
    QVariantList m_cursorReadouts;
    int m_cursorDragIndex = 0;
    QPointF m_dragStartPixel;
    double m_dragStartXMinimum = 0.0, m_dragStartXMaximum = 1.0, m_dragStartYMinimum = -1.0, m_dragStartYMaximum = 1.0;
    bool m_dragging = false;
    bool m_panMoved = false;
    mutable QMutex m_dataMutex;
};
