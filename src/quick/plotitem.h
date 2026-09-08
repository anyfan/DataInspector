#pragma once
#include <QQuickItem>
#include <QColor>
#include <QVector>
#include <QMutex>

class PlotItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(double xMinimum READ xMinimum NOTIFY viewChanged)
    Q_PROPERTY(double xMaximum READ xMaximum NOTIFY viewChanged)
    Q_PROPERTY(double yMinimum READ yMinimum NOTIFY viewChanged)
    Q_PROPERTY(double yMaximum READ yMaximum NOTIFY viewChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(bool cursorEnabled READ cursorEnabled WRITE setCursorEnabled NOTIFY cursorChanged)
    Q_PROPERTY(double cursorX READ cursorX NOTIFY cursorChanged)
public:
    explicit PlotItem(QQuickItem *parent = nullptr);
    double xMinimum() const { return m_xMinimum; }
    double xMaximum() const { return m_xMaximum; }
    double yMinimum() const { return m_yMinimum; }
    double yMaximum() const { return m_yMaximum; }
    double lineWidth() const { return m_lineWidth; }
    bool cursorEnabled() const { return m_cursorEnabled; }
    double cursorX() const { return m_cursorX; }
    void setLineWidth(double width);
    void setCursorEnabled(bool enabled);
    Q_INVOKABLE void setSeries(const QVector<double> &time, const QVector<double> &values);
    Q_INVOKABLE void appendSeries(const QVector<double> &time, const QVector<double> &values, const QColor &color);
    Q_INVOKABLE void clearSeries();
    Q_INVOKABLE void fitView();
signals:
    void viewChanged();
    void lineWidthChanged();
    void cursorChanged();
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
    struct Series {
        QVector<QPointF> points;
        QColor color;
        bool monotonicTime = true;
        mutable double cachedXMinimum = 0.0;
        mutable double cachedXMaximum = 0.0;
        mutable int cachedBuckets = 0;
        mutable QVector<QPointF> cachedLod;
    };
    QVector<QPointF> buildLod(const Series &series) const;
    void setRange(double xMinimum, double xMaximum, double yMinimum, double yMaximum);
    QPointF pixelToData(const QPointF &pixel) const;
    QVector<Series> m_series;
    double m_xMinimum = 0.0, m_xMaximum = 1.0, m_yMinimum = -1.0, m_yMaximum = 1.0;
    double m_lineWidth = 2.0;
    bool m_cursorEnabled = false;
    double m_cursorX = 0.0;
    QPointF m_dragStartPixel;
    double m_dragStartXMinimum = 0.0, m_dragStartXMaximum = 1.0, m_dragStartYMinimum = -1.0, m_dragStartYMaximum = 1.0;
    bool m_dragging = false;
    mutable QMutex m_dataMutex;
};
