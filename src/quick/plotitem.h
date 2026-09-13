#pragma once
#include <QQuickItem>
#include <QColor>
#include <QVector>
#include <QMutex>
#include <QVariantList>
#include <memory>

#include "render/plotseriesstore.h"
#include "render/plotlodbuilder.h"
#include "render/plotlodscheduler.h"

class PlotItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool lodPending READ lodPending NOTIFY lodChanged)
    Q_PROPERTY(int cursorMode READ cursorMode WRITE setCursorMode NOTIFY cursorChanged)
    Q_PROPERTY(double xMinimum READ xMinimum NOTIFY viewChanged)
    Q_PROPERTY(double xMaximum READ xMaximum NOTIFY viewChanged)
    Q_PROPERTY(double yMinimum READ yMinimum NOTIFY viewChanged)
    Q_PROPERTY(double yMaximum READ yMaximum NOTIFY viewChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(bool cursorEnabled READ cursorEnabled WRITE setCursorEnabled NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX READ cursorX NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX1 READ cursorX1 NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX2 READ cursorX2 NOTIFY cursorChanged)
    Q_PROPERTY(double cursorDeltaT READ cursorDeltaT NOTIFY cursorDeltaTChanged)
    Q_PROPERTY(QVariantList xTicks READ xTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList yTicks READ yTicks NOTIFY axisTicksChanged)
    Q_PROPERTY(QVariantList cursorReadouts READ cursorReadouts NOTIFY cursorValuesChanged)
public:
    enum CursorMode { NoCursor = 0, SingleCursor = 1, DoubleCursor = 2 };
    Q_ENUM(CursorMode)
    explicit PlotItem(QQuickItem *parent = nullptr);
    bool lodPending() const { return m_lodScheduler->pending(); }
    double xMinimum() const;
    double xMaximum() const;
    double yMinimum() const;
    double yMaximum() const;
    double lineWidth() const;
    bool cursorEnabled() const;
    double cursorX() const;
    int cursorMode() const;
    double cursorX1() const;
    double cursorX2() const;
    double cursorDeltaT() const;
    QVariantList xTicks() const;
    QVariantList yTicks() const;
    QVariantList cursorReadouts() const;
    void setLineWidth(double width);
    void setCursorEnabled(bool enabled);
    void setCursorMode(int mode);
    void setSeriesStore(const std::shared_ptr<const PlotSeriesStore> &store);
    void setVisibleSeries(const QVector<PlotSeriesId> &orderedIds);
    QVector<PlotSeriesId> visibleSeriesIds() const;
    Q_INVOKABLE void setCursorX(double x, int cursorIndex = 1);
    void setCursorPosition(double x, int cursorIndex = 1);
    Q_INVOKABLE void fitView();
    Q_INVOKABLE void zoomAxis(int axis, double fraction, double steps);
    Q_INVOKABLE void fitY();
    Q_INVOKABLE void setXRange(double xMinimum, double xMaximum);
signals:
    void lodChanged();
    void viewChanged();
    void lineWidthChanged();
    void cursorChanged();
    void cursorDeltaTChanged();
    void cursorValuesChanged();
    void axisTicksChanged();
    void activated();
    void rangeChanged(double xMinimum, double xMaximum, double yMinimum, double yMaximum);
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
private:
    double nearestRawX(double x) const;
    void updateCursorValuesLocked();
    void rebuildTicksLocked();
    bool cursorHit(double pixelX, int *cursorIndex) const;
    void refreshSnapshotLocked();
    void setRange(double xMinimum, double xMaximum, double yMinimum, double yMaximum);
    QPointF pixelToData(const QPointF &pixel) const;
    std::shared_ptr<const PlotSeriesStore> m_seriesStore;
    QVector<PlotSeriesId> m_visibleSeries;
    PlotSeriesSnapshot m_seriesSnapshot;
    PlotLodScheduler *m_lodScheduler;
    std::shared_ptr<const LodResult> m_lodResult;
    void requestLod();
    double m_xMinimum = 0.0, m_xMaximum = 1.0, m_yMinimum = -1.0, m_yMaximum = 1.0;
    double m_lineWidth = 2.0;
    bool m_cursorEnabled = false;
    double m_cursorX = 0.0;
    int m_cursorMode = NoCursor;
    double m_cursorX1 = 0.0;
    double m_cursorX2 = 1.0;
    QVector<QVector<double>> m_cursorValues;
    QVariantList m_xTicks;
    QVariantList m_yTicks;
    QVariantList m_cursorReadouts;
    int m_cursorDragIndex = 0;
    QPointF m_dragStartPixel;
    double m_dragStartXMinimum = 0.0, m_dragStartXMaximum = 1.0, m_dragStartYMinimum = -1.0, m_dragStartYMaximum = 1.0;
    bool m_dragging = false;
    mutable QMutex m_dataMutex;
};
