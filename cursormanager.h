#ifndef CURSORMANAGER_H
#define CURSORMANAGER_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QPen>

// 向前声明
class QCustomPlot;
class QCPGraph;
class QCPItemLine;
class QCPItemTracer;
class QCPItemText;
class QMouseEvent;
class QAction;
class QCPRange;

class CursorManager : public QObject
{
    Q_OBJECT

public:
    enum CursorMode
    {
        NoCursor,
        SingleCursor,
        DoubleCursor
    };
    explicit CursorManager(QList<QCustomPlot *> *plotWidgets,
                           QObject *parent = nullptr);
    ~CursorManager();

    CursorMode getMode() const;
    void setActivePlot(QCustomPlot *plot);

signals:
    void cursorKeyChanged(double key, int cursorIndex);

public slots:
    void onCursorActionTriggered(QAction *action);
    void setMode(CursorManager::CursorMode mode);
    void updateAllCursors();
    void updateCursors(double key, int cursorIndex = 1);
    void clearCursors();
    void setupCursors();

    // QCustomPlot 信号槽
    void onPlotMousePress(QMouseEvent *event);
    void onPlotMouseMove(QMouseEvent *event);
    void onPlotMouseRelease(QMouseEvent *event);

private:
    void resolveLabelOverlaps(QList<QCPItemText *> &labelsOnPlot);
    double snapKeyToData(double key) const;

    struct CursorData
    {
        double key = 0.0;
        bool isDragging = false;

        QList<QCPItemLine *> lines;
        QList<QCPItemText *> xLabels;
        QMap<QCPGraph *, QCPItemTracer *> graphTracers;
        QMap<QCPItemTracer *, QCPItemText *> yLabels;
    };

    CursorMode m_cursorMode;

    // 使用 Vector 替代散乱的 1/2 变量
    // Index 0 -> Cursor 1, Index 1 -> Cursor 2
    QVector<CursorData> m_cursors;

    QList<QCustomPlot *> *m_plotWidgets;
    QCustomPlot *m_currentActivePlot = nullptr;
    // --- 优化部分结束 ---
};

#endif // CURSORMANAGER_H