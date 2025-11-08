#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QVector>
#include <QPen>
#include "datamanager.h"

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
class QPushButton;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QCPItemLine;
class QCPItemTracer;
class QCPItemText;
class QCPRange;

/**
 * @brief 主窗口类，实现 data flow.md 中的核心架构
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // 游标模式枚举
    enum CursorMode
    {
        NoCursor,
        SingleCursor,
        DoubleCursor
    };

signals:
    /**
     * @brief [信号] 请求工作线程加载一个 CSV 文件
     */
    void requestLoadCsv(const QString &filePath);

private slots:
    // 文件菜单动作
    void on_actionLoadFile_triggered();

    // DataManager 信号槽
    void onDataLoadFinished(const CsvData &data);
    void onDataLoadFailed(const QString &errorString);
    void showLoadProgress(int percentage);

    // 布局菜单动作
    void on_actionLayout1x1_triggered();
    void on_actionLayout2x2_triggered();
    void on_actionLayout3x2_triggered();

    // 交互槽
    void onPlotClicked();
    void onSignalItemChanged(QStandardItem *item);
    void onSignalItemDoubleClicked(const QModelIndex &index);

    // 游标和重放槽函数
    void onCursorModeChanged(QAction *action);
    void onReplayActionToggled(bool checked);
    void onPlotMouseMove(QMouseEvent *event);

    // 重放控制槽
    void onPlayPauseClicked();
    void onStepForwardClicked();
    void onStepBackwardClicked();
    void onReplayTimerTimeout();
    void onTimeSliderChanged(int value);

    // 视图缩放槽函数
    /**
     * @brief [槽] 适应视图大小 (所有子图, X 和 Y 轴)
     */
    void on_actionFitView_triggered();
    /**
     * @brief [槽] 适应视图大小 (所有子图, 仅 X 轴)
     */
    void on_actionFitViewTime_triggered();
    /**
     * @brief [槽] 适应视图大小 (仅活动子图, 仅 Y 轴)
     */
    void on_actionFitViewY_triggered();

    // --- 新增：X轴同步槽 ---
    /**
     * @brief [槽] 当一个X轴范围改变时，同步所有其他的X轴
     * @param newRange 新的X轴范围
     */
    void onXAxisRangeChanged(const QCPRange &newRange);
    // --- -------------------- ---

private:
    // UI 创建
    void createActions();
    void createMenus();
    void createDocks();
    void createToolBars();
    void createReplayDock();

    /**
     * @brief 设置数据管理工作线程
     */
    void setupDataManagerThread();

    /**
     * @brief 设置中央绘图区域的布局 (如 2x2)
     */
    void setupPlotLayout(int rows, int cols);

    /**
     * @brief 清理当前的绘图布局
     */
    void clearPlotLayout();

    /**
     * @brief 使用加载的数据填充信号树
     */
    void populateSignalTree(const CsvData &data);

    /**
     * @brief 为新创建的 plot 设置标准交互
     */
    void setupPlotInteractions(QCustomPlot *plot);

    /**
     * @brief [新增] 根据 m_activePlot 更新信号树的勾选状态
     */
    void updateSignalTreeChecks();

    // --- 新增：游标和重放辅助函数 ---
    /**
     * @brief 销毁所有游标项
     */
    void clearCursors();

    /**
     * @brief 根据 m_cursorMode 创建游标项 (lines, tracers)
     */
    void setupCursors();

    /**
     * @brief 核心同步逻辑：更新所有游标到指定的 key
     * @param key 新的 X 轴坐标
     * @param cursorIndex 要移动的游标 (1 或 2)
     */
    void updateCursors(double key, int cursorIndex = 1);

    /**
     * @brief 更新重放控件的状态 (滑块范围、标签)
     */
    void updateReplayControls();

    /**
     * @brief 获取已加载数据的全局时间范围
     */
    QCPRange getGlobalTimeRange() const;

    /**
     * @brief 估算数据的时间步长 (用于步进)
     */
    double findDataTimeStep() const;
    // --- ---------------------- ---

    // --- 工作线程 ---
    QThread *m_dataThread;
    DataManager *m_dataManager;

    // --- UI 控件 ---
    QWidget *m_plotContainer;  // 中央控件
    QDockWidget *m_signalDock; // 左侧停靠栏
    QTreeView *m_signalTree;   // 信号列表
    QStandardItemModel *m_signalTreeModel;
    QProgressDialog *m_progressDialog;

    // --- 绘图管理 ---
    QList<QCustomPlot *> m_plotWidgets; // 存储所有 plot 实例
    QCustomPlot *m_activePlot;          // 当前选中的 plot

    // (Plot -> (SignalIndex -> Graph)) 映射
    // 用于跟踪*哪个plot上*有*哪些graph*
    QMap<QCustomPlot *, QMap<int, QCPGraph *>> m_plotGraphMap;
    QMap<QCustomPlot *, QFrame *> m_plotFrameMap; // 跟踪 plot 和它的高亮 frame
    QCustomPlot *m_lastMousePlot;                 // 跟踪最后一次鼠标事件的 plot

    // --- 菜单和工具栏动作 ---
    QAction *m_loadFileAction;
    QAction *m_layout1x1Action;
    QAction *m_layout2x2Action;
    QAction *m_layout3x2Action;

    QToolBar *m_viewToolBar;
    QAction *m_cursorNoneAction;
    QAction *m_cursorSingleAction;
    QAction *m_cursorDoubleAction;
    QAction *m_replayAction;
    QActionGroup *m_cursorGroup;

    // --- 新增：视图缩放动作 ---
    QAction *m_fitViewAction;
    QAction *m_fitViewTimeAction;
    QAction *m_fitViewYAction;
    // --- -------------------- ---

    // --- 游标状态 ---
    CursorMode m_cursorMode;
    double m_cursorKey1;
    double m_cursorKey2;
    QList<QCPItemLine *> m_cursorLines1;
    QList<QCPItemLine *> m_cursorLines2;
    QList<QCPItemText *> m_cursorLabels1;
    QList<QCPItemText *> m_cursorLabels2;
    // (Graph -> Tracer) 映射
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers1;
    QMap<QCPGraph *, QCPItemTracer *> m_graphTracers2;

    // --- 重放控制 ---
    QDockWidget *m_replayDock;
    QWidget *m_replayWidget;
    QPushButton *m_playPauseButton;
    QPushButton *m_stepForwardButton;
    QPushButton *m_stepBackwardButton;
    QDoubleSpinBox *m_speedSpinBox;
    QSlider *m_timeSlider;
    QLabel *m_currentTimeLabel;
    QTimer *m_replayTimer;

    // --- 加载的数据缓存 ---
    QVector<double> m_loadedTimeData;
    QVector<QVector<double>> m_loadedValueData;
    QVector<QPen> m_signalPens;
};

#endif // MAINWINDOW_H