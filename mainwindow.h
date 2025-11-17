#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPen>
#include <QSet>
#include "datamanager.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>

#include "cursormanager.h"
#include "replaymanager.h"

// 向前声明
class QCustomPlot;
class QAction;
class QThread;
class QDockWidget;
class QTreeView;
class QStandardItemModel;
class QStandardItem;
class QWidget;
class QProgressDialog;
class QModelIndex;
class QCPGraph;
class QFrame;
class QToolBar;
class QActionGroup;
class QTimer;
class QCPRange;
class QCPLegend;
class QCPAbstractLegendItem;
class QCPMarginGroup;
class QDialog;
class QSpinBox;
class QLineEdit;

// 为信号树条目定义自定义数据角色
// 存储唯一的 "filename/tablename/signalindex" 字符串 ID
const int UniqueIdRole = Qt::UserRole + 1;
// 存储布尔值，是否为顶层文件条目
const int IsFileItemRole = Qt::UserRole + 2;
// 存储画笔 (用于信号条目)
const int PenDataRole = Qt::UserRole + 3;
// 存储 QString 文件名 (用于文件、表和信号条目)
const int FileNameRole = Qt::UserRole + 4;
// 存储布尔值，是否为信号条目 (用于委托)
const int IsSignalItemRole = Qt::UserRole + 5;

/**
 * @brief 主窗口类，实现 data flow.md 中的核心架构
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    /**
     * @brief [信号] 请求工作线程加载一个 CSV 文件
     */
    void requestLoadCsv(const QString &filePath);

    /**
     * @brief [信号] 请求工作线程加载一个 MAT 文件
     */
    void requestLoadMat(const QString &filePath);

private slots:
    // 文件菜单动作
    void on_actionLoadFile_triggered();

    // DataManager 信号槽
    void onDataLoadFinished(const FileData &data);
    void onDataLoadFailed(const QString &filePath, const QString &errorString);
    void showLoadProgress(int percentage);

    // 布局菜单动作
    void on_actionLayout1x1_triggered();
    void on_actionLayout1x2_triggered();
    void on_actionLayout2x1_triggered();
    void on_actionLayout2x2_triggered();
    void on_actionLayoutSplitBottom_triggered();
    void on_actionLayoutSplitLeft_triggered();
    void on_actionLayoutSplitTop_triggered();
    void on_actionLayoutSplitRight_triggered();
    void on_actionLayoutCustom_triggered();

    // 交互槽
    void onPlotClicked();
    void onSignalItemChanged(QStandardItem *item);
    void onSignalItemDoubleClicked(const QModelIndex &index);
    // 图例交互槽
    void onLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event);
    void onLegendContextMenu(const QPoint &pos);
    void onDeleteSignalAction();
    void onDeleteSubplotAction();
    // 信号树的右键菜单槽 ---
    void onSignalTreeContextMenu(const QPoint &pos);
    void onDeleteFileAction();
    // --- ------------------------- ---

    // 重放槽函数
    void onReplayActionToggled(bool checked);

    // 视图缩放槽函数
    void on_actionFitView_triggered();
    void on_actionFitViewTime_triggered();
    void on_actionFitViewY_triggered();

    // X轴同步槽
    void onXAxisRangeChanged(const QCPRange &newRange);
    // 用于在布局更改后更新游标的槽 ---
    void updateCursorsForLayoutChange();

    void onSignalSearchChanged(const QString &text);

    // 用于在子图选中时同步信号树的槽 ---
    void onPlotSelectionChanged();

    // 图例切换槽 ---
    void on_actionToggleLegend_toggled(bool checked);
    // --- ----------------- ---

    // 重写拖放事件处理函数 ---
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // --- 设置活动子图的辅助函数 ---
    void setActivePlot(QCustomPlot *plot);

    // 添加/删除信号的辅助函数 ---
    void addSignalToPlot(const QString &uniqueID, QCustomPlot *plot);
    void removeSignalFromPlot(const QString &uniqueID, QCustomPlot *plot);

    // UI 创建
    void createActions();
    void createMenus();
    void createDocks();
    void createToolBars();

    /**
     * @brief 设置数据管理工作线程
     */
    void setupDataManagerThread();

    /**
     * @brief 设置中央绘图区域的布局 (如 2x2)
     */
    void setupPlotLayout(int rows, int cols);

    /**
     * @brief 核心布局函数，使用 QRect 列表创建网格
     */
    void setupPlotLayout(const QList<QRect> &geometries);

    /**
     * @brief 清理当前的绘图布局
     */
    void clearPlotLayout();

    /**
     * @brief 使用加载的数据填充信号树
     */
    void populateSignalTree(const FileData &data);

    /**
     * @brief 为新创建的 plot 设置标准交互
     */
    void setupPlotInteractions(QCustomPlot *plot);

    /**
     * @brief 根据 m_activePlot 更新信号树的勾选状态
     */
    void updateSignalTreeChecks();

    /**
     * @brief 获取已加载数据的全局时间范围
     */
    QCPRange getGlobalTimeRange() const;

    /**
     * @brief 估算数据的时间步长 (用于步进)
     */
    double getSmallestTimeStep() const; // 获取最小步长

    /**
     * @brief 辅助函数，用于将数据范围推送到 ReplayManager
     */
    void updateReplayManagerRange();

    // --- 辅助函数 ---
    /**
     * @brief  从 m_plotGraphMap 中安全地获取一个 QCPGraph*
     */
    QCPGraph *getGraph(QCustomPlot *plot, const QString &uniqueID) const;
    /**
     * @brief  从 item 构建 uniqueID
     */
    QString getUniqueID(QStandardItem *item) const;
    /**
     * @brief 移除一个文件的所有相关数据和图表
     */
    void removeFile(const QString &filename);

    /**
     * @brief 启动加载单个文件的辅助函数
     * @param filePath 要加载的文件的路径
     */
    void loadFile(const QString &filePath);

    /**
     * @brief 递归辅助函数，用于过滤信号树
     * @param item 要检查的当前 QStandardItem
     * @param query 小写的搜索查询
     * @return true 如果此项或其任何子项匹配查询，则返回
     */
    bool filterSignalTree(QStandardItem *item, const QString &query);
    // --- ---------------------- ---

    // --- 工作线程 ---
    QThread *m_dataThread;
    DataManager *m_dataManager;

    // --- UI 控件 ---
    QWidget *m_plotContainer;  // 中央控件
    QDockWidget *m_signalDock; // 左侧停靠栏
    QTreeView *m_signalTree;   // 信号列表
    QStandardItemModel *m_signalTreeModel;
    QLineEdit *m_signalSearchBox;
    QProgressDialog *m_progressDialog;

    // --- 绘图管理 ---
    QList<QCustomPlot *> m_plotWidgets; // 存储所有 plot 实例
    QCustomPlot *m_activePlot;          // 当前选中的 plot

    // (Plot -> (UniqueID -> Graph)) 映射
    // UniqueID 是 "filename/tablename/signalindex" 格式的字符串
    QMap<QCustomPlot *, QMap<QString, QCPGraph *>> m_plotGraphMap;

    // (PlotIndex -> QSet<UniqueID>) 映射
    // UniqueID 是 "filename/tablename/signalindex" 格式的字符串
    QMap<int, QSet<QString>> m_plotSignalMap;

    // (Plot -> PlotIndex) 映射
    // 用于*运行时*快速查找 plot 的索引
    QMap<QCustomPlot *, int> m_plotWidgetMap;

    QMap<QCustomPlot *, QFrame *> m_plotFrameMap; // 跟踪 plot 和它的高亮 frame
    QCustomPlot *m_lastMousePlot;                 // 跟踪最后一次鼠标事件的 plot

    // --- 菜单和工具栏动作 ---
    QAction *m_loadFileAction;

    QAction *m_layout1x1Action;
    QAction *m_layout1x2Action;
    QAction *m_layout2x1Action;
    QAction *m_layout2x2Action;
    QAction *m_layoutSplitBottomAction;
    QAction *m_layoutSplitLeftAction;
    QAction *m_layoutSplitTopAction;
    QAction *m_layoutSplitRightAction;
    QAction *m_layoutCustomAction;

    QToolBar *m_viewToolBar;
    QAction *m_cursorNoneAction;
    QAction *m_cursorSingleAction;
    QAction *m_cursorDoubleAction;
    QAction *m_replayAction;
    QActionGroup *m_cursorGroup;

    // --- 视图缩放动作 ---
    QAction *m_fitViewAction;
    QAction *m_fitViewTimeAction;
    QAction *m_fitViewYAction;

    // 图例切换动作 ---
    QAction *m_toggleLegendAction;

    // 自定义布局对话框的控件指针
    QDialog *m_customLayoutDialog;
    QSpinBox *m_customRowsSpinBox;
    QSpinBox *m_customColsSpinBox;
    // --- -------------------- ---

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

    // 游标管理器
    CursorManager *m_cursorManager;

    // 重放管理器
    ReplayManager *m_replayManager;

    // --- Y轴对齐组 ---
    QCPMarginGroup *m_yAxisGroup; //用于Y轴自动对齐

    // --- 加载的数据缓存 使用 QMap 存储多个文件数据 ---
    QMap<QString, FileData> m_fileDataMap;

    // --- 用于颜色循环的成员 ---
    QVector<QColor> m_colorList; // 预定义的颜色列表
    int m_colorIndex;            // 用于循环颜色列表的索引
};

#endif // MAINWINDOW_H