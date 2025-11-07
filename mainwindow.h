#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QVector> // <-- 新增
#include <QPen>    // <-- 新增
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

private:
    // UI 创建
    void createActions();
    void createMenus();
    void createDocks();

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
    QMap<QCustomPlot *, QMap<int, QCPGraph *>> m_plotGraphMap; // <-- 重构

    // --- 菜单动作 ---
    QAction *m_loadFileAction;
    QAction *m_layout1x1Action;
    QAction *m_layout2x2Action;
    QAction *m_layout3x2Action;

    // --- 加载的数据缓存 ---
    QVector<double> m_loadedTimeData;
    QVector<QVector<double>> m_loadedValueData;
    QVector<QPen> m_signalPens; // <-- 新增: 存储每个信号的预定义样式
};

#endif // MAINWINDOW_H