// Curve picking, panning, wheel zoom and pointer/cursor dragging.
#include "plotitem.h"
#include <QMutexLocker>
#include <QtMath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <limits>

void PlotItem::selectSeriesAt(double pixelX, double pixelY)
{
    int selected = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    {
        QMutexLocker lock(&m_dataMutex);
        if (!m_lodResult || width() <= 0 || height() <= 0
            || pixelX < 0 || pixelX > width() || pixelY < 0 || pixelY > height()) return;
        const QPointF click(pixelX, pixelY);
        for (const auto &segment : m_lodResult->segments) {
            if (!m_visibleSeries.contains(segment.seriesId) || segment.lineStyle == Qt::NoPen) continue;
            const double tolerance = 5.0 + (segment.lineWidth > 0 ? segment.lineWidth : m_lineWidth) / 2.0;
            QPointF previous;
            bool havePrevious = false;
            for (const auto &sample : segment.points) {
                const QPointF point((sample.x() - m_xMinimum) / (m_xMaximum - m_xMinimum) * width(),
                    (m_yMaximum - normalizedYLocked(segment.seriesId, sample.y()))
                        / (m_yMaximum - m_yMinimum) * height());
                if (!qIsFinite(point.x()) || !qIsFinite(point.y())) { havePrevious = false; continue; }
                if (havePrevious) {
                    const QPointF delta = point - previous;
                    const double lengthSquared = QPointF::dotProduct(delta, delta);
                    const double fraction = lengthSquared > 0
                        ? qBound(0.0, QPointF::dotProduct(click - previous, delta) / lengthSquared, 1.0) : 0;
                    const QPointF offset = click - (previous + fraction * delta);
                    const double distance = QPointF::dotProduct(offset, offset);
                    if (distance <= tolerance * tolerance && distance <= bestDistance) {
                        bestDistance = distance;
                        selected = segment.seriesId;
                    }
                }
                previous = point;
                havePrevious = true;
            }
        }
    }
    if (selected >= 0) {
        setHighlightedSeries(selected);
        emit seriesClicked(selected);
    }
}

void PlotItem::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton && e->button() != Qt::MiddleButton) return;
    emit activated();
    int cursorIndex = 0;
    {
        QMutexLocker lock(&m_dataMutex);
        if (cursorHit(e->position().x(), &cursorIndex)) {
            m_cursorDragIndex = cursorIndex;
            m_dragging = false;
            e->accept();
            return;
        }
        // The range members are written under the lock (setRange may run from
        // range syncing while a press is delivered), so snapshot them here
        // instead of reading them unguarded.
        m_dragStartXMinimum = m_xMinimum;
        m_dragStartXMaximum = m_xMaximum;
        m_dragStartYMinimum = m_yMinimum;
        m_dragStartYMaximum = m_yMaximum;
    }
    m_cursorDragIndex = 0;
    m_dragging = true;
    m_panMoved = false;
    m_dragStartPixel = e->position();
    emit viewInteractionStarted();
    e->accept();
}

void PlotItem::mouseMoveEvent(QMouseEvent *e)
{
    if (m_cursorDragIndex) {
        const double x = pixelToData(e->position()).x();
        if (cursorMode() == DoubleCursor) {
            // Cursors never cross: dragging one past the other hands the drag to the other cursor.
            double x1, x2;
            { QMutexLocker lock(&m_dataMutex); x1 = m_cursorX1; x2 = m_cursorX2; }
            if (m_cursorDragIndex == 1 && x > x2) { setCursorX(x2, 1); m_cursorDragIndex = 2; }
            else if (m_cursorDragIndex == 2 && x < x1) { setCursorX(x1, 2); m_cursorDragIndex = 1; }
        }
        setCursorX(x, m_cursorDragIndex);
        e->accept();
        return;
    }
    if (!m_dragging) return;
    QPointF d = e->position() - m_dragStartPixel;
    if (!m_panMoved && QPointF::dotProduct(d, d) < 16) return;
    m_panMoved = true;
    double xs = m_dragStartXMaximum - m_dragStartXMinimum, ys = m_dragStartYMaximum - m_dragStartYMinimum;
    setRange(m_dragStartXMinimum - d.x() / qMax(width(), 1.) * xs, m_dragStartXMaximum - d.x() / qMax(width(), 1.) * xs,
             m_dragStartYMinimum + d.y() / qMax(height(), 1.) * ys, m_dragStartYMaximum + d.y() / qMax(height(), 1.) * ys);
    e->accept();
}

void PlotItem::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging && !m_panMoved && e->button() == Qt::LeftButton)
        selectSeriesAt(e->position().x(), e->position().y());
    if (m_dragging) emit viewInteractionFinished();
    m_dragging = false;
    m_cursorDragIndex = 0;
    e->accept();
}

void PlotItem::mouseUngrabEvent()
{
    if (m_dragging) emit viewInteractionFinished();
    m_dragging = false;
    m_cursorDragIndex = 0;
}

void PlotItem::wheelEvent(QWheelEvent *e)
{
    // Zoom both axes around the pointer position.
    const double factor = e->angleDelta().y() > 0 ? .85 : 1 / .85;
    const QPointF anchor = pixelToData(e->position());
    setRange(anchor.x() - (anchor.x() - m_xMinimum) * factor,
             anchor.x() + (m_xMaximum - anchor.x()) * factor,
             anchor.y() - (anchor.y() - m_yMinimum) * factor,
             anchor.y() + (m_yMaximum - anchor.y()) * factor);
    e->accept();
}

QPointF PlotItem::pixelToData(const QPointF &p) const
{
    QMutexLocker lock(&m_dataMutex);
    return {m_xMinimum + p.x() / qMax(width(), 1.) * (m_xMaximum - m_xMinimum),
            m_yMaximum - p.y() / qMax(height(), 1.) * (m_yMaximum - m_yMinimum)};
}
