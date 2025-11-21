#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Qt Headers
#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QVector>
#include <QSet>
#include <QDomDocument>

// Local Headers
#include "datamanager.h"
#include "cursormanager.h"
#include "replaymanager.h"

// Forward Declarations
class QCustomPlot;
class QCPGraph;
class QCPRange;
class QCPLegend;
class QCPAbstractLegendItem;
class QCPMarginGroup;
class QCPItemLine;
class QCPItemText;
class QCPItemTracer;
class QStandardItemModel;
class QStandardItem;
class QTreeView;
class QDockWidget;
class QProgressDialog;
class QLineEdit;
class QSpinBox;
class QThread;

// Custom Roles
enum TreeItemRoles
{
    UniqueIdRole = Qt::UserRole + 1, // "filename/tablename/signalindex"
    IsFileItemRole,
    PenDataRole,
    FileNameRole,
    IsSignalItemRole
};

struct SignalLocation
{
    const SignalTable *table = nullptr;
    int signalIndex = -1;
    QString name;
    QPen pen;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void requestLoadCsv(const QString &filePath);
    void requestLoadMat(const QString &filePath);

protected:
    // Event Overrides
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    //  1. 菜单动作槽 (Menu Actions)
    void on_actionLoadFile_triggered();
    void on_actionImportView_triggered();
    void on_actionToggleLegend_toggled(bool checked);
    void onLegendPositionChanged(QAction *action);
    void on_actionClearAllPlots_triggered();
    void onOpenGLActionToggled(bool checked);

    // 布局动作
    void onLayoutActionTriggered();
    void on_actionLayoutCustom_triggered();

    // 视图/缩放动作
    void on_actionFitView_triggered();
    void on_actionFitViewTime_triggered();
    void on_actionFitViewY_triggered();
    void on_actionFitViewYAll_triggered();

    //  2. 数据加载槽 (Data Loading)
    void onDataLoadFinished(const FileData &data);
    void onDataLoadFailed(const QString &filePath, const QString &errorString);
    void showLoadProgress(int percentage);

    //  3. 信号树交互槽 (Signal Tree)
    void onSignalItemChanged(QStandardItem *item);
    void onSignalItemDoubleClicked(const QModelIndex &index);
    void onSignalSearchChanged(const QString &text);
    void onSignalTreeContextMenu(const QPoint &pos);
    void onDeleteFileAction(); // 树右键删除文件

    //  4. 绘图与交互槽 (Plotting & Interaction)
    void onPlotClicked();
    void onPlotSelectionChanged();
    void onXAxisRangeChanged(const QCPRange &newRange);

    // 图例与图表右键
    void onLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event);
    void onLegendContextMenu(const QPoint &pos);
    void onDeleteSignalAction();
    void onDeleteSubplotAction();

    // 游标与重放
    void onReplayActionToggled(bool checked);
    void updateCursorsForLayoutChange();

private:
    //  内部数据结构
    struct LayoutInfo
    {
        int rows = 1;
        int cols = 1;
        QString layoutType = "grid";
    };
    struct SignalInfo
    {
        QString name;
        int id = 0;
        QColor color;
        QList<int> plotIds;
    };
    enum FitTarget
    {
        FitActivePlot, // 仅当前活动子图
        FitAllPlots    // 所有子图
    };

    //  初始化函数
    void setupDataManagerThread();
    void createActions();
    void createMenus();
    void createToolBars();
    void createDocks();
    void setupPlotLayout(int rows, int cols);
    void setupPlotLayout(const QList<QRect> &geometries);
    void setupPlotInteractions(QCustomPlot *plot);
    void clearPlotLayout();

    void setupGraphInstance(QCustomPlot *plot, const QString &uniqueID, const SignalLocation &loc);

    //  核心逻辑辅助函数
    void loadFile(const QString &filePath);
    void importView(const QString &filePath);
    void removeFile(const QString &filename);

    // 信号管理
    void addSignalToPlot(const QString &uniqueID, QCustomPlot *plot, bool replot = true);
    void removeSignalFromPlot(const QString &uniqueID, QCustomPlot *plot);
    void populateSignalTree(const FileData &data);
    void updateSignalTreeChecks();
    bool filterSignalTree(QStandardItem *item, const QString &query);

    // 绘图管理
    void setActivePlot(QCustomPlot *plot);
    QCPGraph *getGraph(QCustomPlot *plot, const QString &uniqueID) const;
    QStandardItem *findItemBySignalName(const QString &name);

    // 数据辅助
    QCPRange getGlobalTimeRange() const;
    double getSmallestTimeStep() const;
    void updateReplayManagerRange();
    QString getUniqueID(QStandardItem *item) const;

    // 导入辅助
    LayoutInfo parseViewMetaData(const QDomDocument &doc);
    QList<SignalInfo> parseCheckedSignals(const QDomDocument &doc);
    void applyImportedView(const LayoutInfo &layout, const QList<SignalInfo> &signalList);
    // 针对单个 Plot 配置图例的辅助函数
    void configurePlotLegend(QCustomPlot *plot, int mode);

    SignalLocation getSignalDataFromID(const QString &uniqueID) const;

    /**
     * @brief 通用视图自适应函数
     * @param fitX 是否缩放 X 轴
     * @param fitY 是否缩放 Y 轴
     * @param target 目标范围 (仅活动图表 或 所有图表)
     */
    void performFitView(bool fitX, bool fitY, FitTarget target);

    //  成员变量 (分组)

    // 1. 核心逻辑组件
    QThread *m_dataThread;
    DataManager *m_dataManager;
    CursorManager *m_cursorManager;
    ReplayManager *m_replayManager;

    // 2. 主 UI 容器
    QWidget *m_plotContainer;
    QDockWidget *m_signalDock;
    QTreeView *m_signalTree;
    QStandardItemModel *m_signalTreeModel;
    QLineEdit *m_signalSearchBox;
    QProgressDialog *m_progressDialog;
    QToolBar *m_viewToolBar;
    QDialog *m_customLayoutDialog; // 懒加载
    QSpinBox *m_customRowsSpinBox;
    QSpinBox *m_customColsSpinBox;

    // 3. 绘图状态管理
    QList<QCustomPlot *> m_plotWidgets; // 所有 Plot 列表
    QCustomPlot *m_activePlot;          // 当前选中的 Plot
    QCustomPlot *m_lastMousePlot;       // 最后交互的 Plot (用于游标吸附)

    // 信号映射 (PlotIndex -> Set<SignalID>) - 用于持久化
    QMap<int, QSet<QString>> m_plotSignalMap;

    //  快速查找表
    QHash<QString, QStandardItem *> m_uniqueIdMap;

    QCPMarginGroup *m_yAxisGroup; // Y轴对齐

    // 4. 数据缓存
    QMap<QString, FileData> m_fileDataMap;
    QVector<QColor> m_colorList;
    int m_colorIndex;

    // 5. 动作 (Actions)
    QAction *m_loadFileAction;
    QAction *m_importViewAction;
    //  图例位置动作组
    QActionGroup *m_legendPosGroup;
    QAction *m_legendPosOutsideTopAction;
    QAction *m_legendPosInsideTLAction;
    QAction *m_legendPosInsideTRAction;
    // 布局
    QAction *m_layout1x1Action;
    QAction *m_layout1x2Action;
    QAction *m_layout2x1Action;
    QAction *m_layout2x2Action;
    QAction *m_layoutSplitBottomAction;
    QAction *m_layoutSplitLeftAction;
    QAction *m_layoutSplitTopAction;
    QAction *m_layoutSplitRightAction;
    QAction *m_layoutCustomAction;
    // 视图 & 工具
    QAction *m_fitViewAction;
    QAction *m_fitViewTimeAction;
    QAction *m_fitViewYAction;
    QAction *m_fitViewYAllAction;
    QAction *m_toggleLegendAction;
    QAction *m_openGLAction;
    QAction *m_clearAllPlotsAction;
    // 游标
    QAction *m_cursorNoneAction;
    QAction *m_cursorSingleAction;
    QAction *m_cursorDoubleAction;
    QActionGroup *m_cursorGroup;
    QAction *m_replayAction;
};

#endif // MAINWINDOW_H