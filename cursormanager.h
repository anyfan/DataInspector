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

    /**
     * @brief 构造函数
     * @param plotGraphMap 指向 MainWindow 的图表映射
     * @param plotWidgets 指向 MainWindow 的子图列表
     * @param lastMousePlotPtr 指向 MainWindow 的 m_lastMousePlot 指针的指针 (用于同步)
     * @param parent
     */
    explicit CursorManager(QMap<QCustomPlot *, QMap<QString, QCPGraph *>> *plotGraphMap,
                           QList<QCustomPlot *> *plotWidgets,
                           QCustomPlot **lastMousePlotPtr,
                           QObject *parent = nullptr);
    ~CursorManager();

    /**
     * @brief 获取当前游标模式
     */
    CursorMode getMode() const;

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
     * @brief [新增][槽] 以编程方式设置游标模式
     */
    void setMode(CursorManager::CursorMode mode);

    /**
     * @brief [新增][槽] 使用内部键值强制更新所有游标
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

    // --- QCustomPlot 信号槽 ---

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
    // --- 游标状态 ---
    CursorMode m_cursorMode;
    double m_cursorKey1;
    double m_cursorKey2;
    bool m_isDraggingCursor1; // 拖拽状态
    bool m_isDraggingCursor2; // 拖拽状态

    QList<QCPItemLine *> m_cursorLines1;
    QList<QCPItemLine *> m_cursorLines2;
    QList<QCPItemText *> m_cursorXLabels1; // 用于 X 轴标签
    QList<QCPItemText *> m_cursorXLabels2; // 用于 X 轴标签

    // (Graph -> Tracer) 映射
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers1;
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers2;
    // (Tracer -> Label) 映射
    QMap<QCPItemTracer *, QCPItemText *> m_cursorYLabels1; // 用于 Y 轴标签
    QMap<QCPItemTracer *, QCPItemText *> m_cursorYLabels2; // 用于 Y 轴标签

    // --- 指向 MainWindow 成员的指针 ---
    // (我们不拥有这些对象，只使用它们)
    QMap<QCustomPlot *, QMap<QString, QCPGraph *>> *m_plotGraphMap;
    QList<QCustomPlot *> *m_plotWidgets;
    QCustomPlot **m_lastMousePlotPtr; // 指向 m_lastMousePlot 指针的指针
};

#endif // CURSORMANAGER_H