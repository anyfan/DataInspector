#pragma once
#include <QQuickItem>
#include <QMutex>
#include <QTimer>
#include "render/trajectorybuilder.h"

class TrajectoryItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY previewChanged)
    Q_PROPERTY(QString error READ error NOTIFY previewChanged)
    Q_PROPERTY(bool planar READ planar NOTIFY previewChanged)
    Q_PROPERTY(QVariantList axisLabels READ axisLabels NOTIFY previewChanged)
    Q_PROPERTY(QRectF orientationRect READ orientationRect NOTIFY previewChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY markersChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY styleChanged)
    Q_PROPERTY(QColor axisColor READ axisColor WRITE setAxisColor NOTIFY styleChanged)
public:
    explicit TrajectoryItem(QQuickItem *parent = nullptr);
    ~TrajectoryItem() override;
    void setAxes(const std::array<PlotSeriesDataPtr, 3> &axes, bool geographic = false);
    void setTimeCursor(int mode, double t1, double t2, double minimum, double maximum);
    TrajectoryCamera camera() const;
    void setCamera(const TrajectoryCamera &camera);
    Q_INVOKABLE void fitView();
    Q_INVOKABLE void presetView(int preset);
    Q_INVOKABLE bool beginDrag(const QPointF &position, bool rotate);
    Q_INVOKABLE void dragTo(const QPointF &position);
    Q_INVOKABLE void endDrag();
    Q_INVOKABLE void zoomAt(const QPointF &position, double wheelDelta);
    bool pending() const { return bool(m_job); }
    QString error() const;
    bool planar() const;
    QVariantList axisLabels() const;
    QRectF orientationRect() const;
    QVariantList markers() const;
    double lineWidth() const;
    void setLineWidth(double width);
    QColor axisColor() const;
    void setAxisColor(const QColor &color);
signals:
    void activated();
    void previewChanged();
    void markersChanged();
    void cameraChanged();
    void cameraAboutToChange();
    void viewInteractionStarted();
    void viewInteractionFinished();
    void styleChanged();
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void requestPreview();
    void start();
    void finish();
    struct Job {
        std::atomic_bool cancelled{false}, done{false};
        quint64 revision = 0;
        std::shared_ptr<const TrajectoryData> data;
        std::shared_ptr<const TrajectoryPreview> preview;
        TrajectoryCamera camera;
        QSizeF size;
    };
    // Jobs are GUI-owned; the worker only writes its private job before release/done.
    std::shared_ptr<Job> m_job;
    QTimer m_poll;
    quint64 m_revision = 0;
    std::array<PlotSeriesDataPtr, 3> m_axes;
    bool m_geographic = false; // GUI-owned, captured by value for background jobs
    mutable QMutex m_mutex;
    std::shared_ptr<const TrajectoryData> m_data;
    std::shared_ptr<const TrajectoryPreview> m_preview;
    TrajectoryCamera m_camera, m_displayCamera, m_dragCamera;
    QSizeF m_displaySize;
    QSizeF m_dragSize;
    double m_dragScale = 1;
    double m_lineWidth = 2;
    QColor m_axisColor = QColor("#9aa8b6");
    int m_cursorMode = 0;
    double m_t1 = 0, m_t2 = 1, m_timeMinimum = 0, m_timeMaximum = 1;
    bool m_dragging = false, m_pan = false;
    QPointF m_dragStart;
};
