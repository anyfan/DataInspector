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

/**
 * @brief 游标管理器
 * * 负责处理所有与数据游标相关的UI和逻辑
 * * 包括创建、销毁、拖拽、吸附和标签更新
 */
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

    /**
     * @brief 获取当前游标模式
     */
    CursorMode getMode() const;

    // 显式通知当前活动的 Plot
    void setActivePlot(QCustomPlot *plot);

signals:
    /**
     * @brief 当游标的 X 轴键值 (时间) 发生变化时发出
     * @param key 新的 X 轴坐标
     * @param cursorIndex 发生变化的游标 (1 或 2)
     */
    void cursorKeyChanged(double key, int cursorIndex);

public slots:
    /**
     * @brief [槽] 响应工具栏动作组，设置游标模式
     */
    void onCursorActionTriggered(QAction *action);

    /**
     * @brief [槽] 以编程方式设置游标模式
     */
    void setMode(CursorManager::CursorMode mode);

    /**
     * @brief [槽] 使用内部键值强制更新所有游标
     * (用于布局更改、添加/删除图表等)
     */
    void updateAllCursors();

    /**
     * @brief [槽] 核心同步逻辑：更新所有游标到指定的 key
     * @param key 新的 X 轴坐标
     * @param cursorIndex 要移动的游标 (1 或 2)
     */
    void updateCursors(double key, int cursorIndex = 1);

    /**
     * @brief [槽] 销毁所有游标项
     */
    void clearCursors();

    /**
     * @brief [槽] 根据 m_cursorMode 创建游标项 (lines, tracers)
     */
    void setupCursors();

    //  QCustomPlot 信号槽

    /**
     * @brief [槽] 响应 Plot 上的鼠标按下
     */
    void onPlotMousePress(QMouseEvent *event);

    /**
     * @brief [槽] 响应 Plot 上的鼠标移动
     */
    void onPlotMouseMove(QMouseEvent *event);

    /**
     * @brief [槽] 响应 Plot 上的鼠标释放
     */
    void onPlotMouseRelease(QMouseEvent *event);

private:
    /**
     * @brief [辅助] 解析并堆叠重叠的Y轴游标标签
     * @param labelsOnPlot 此子图上此游标的所有 Y 轴标签
     */
    void resolveLabelOverlaps(QList<QCPItemText *> &labelsOnPlot);

    /**
     * @brief [辅助] 将一个 key (X坐标) 吸附到活动子图上的最近数据点
     * @param key 要吸附的原始 key
     * @return 吸附后的 key
     */
    double snapKeyToData(double key) const;

    CursorMode m_cursorMode;
    double m_cursorKey1;
    double m_cursorKey2;
    bool m_isDraggingCursor1;
    bool m_isDraggingCursor2;

    QList<QCPItemLine *> m_cursorLines1;
    QList<QCPItemLine *> m_cursorLines2;
    QList<QCPItemText *> m_cursorXLabels1;
    QList<QCPItemText *> m_cursorXLabels2;

    // (Graph -> Tracer) 映射
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers1;
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers2;
    // (Tracer -> Label) 映射
    QMap<QCPItemTracer *, QCPItemText *> m_cursorYLabels1;
    QMap<QCPItemTracer *, QCPItemText *> m_cursorYLabels2;

    // 修改：移除了 m_plotGraphMap 和 m_lastMousePlotPtr
    QList<QCustomPlot *> *m_plotWidgets;
    QCustomPlot *m_currentActivePlot = nullptr;
};

#endif // CURSORMANAGER_H