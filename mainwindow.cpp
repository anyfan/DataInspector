#include "mainwindow.h"
#include "qcustomplot.h"
// #include "csvloader.h" // <-- 已删除

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QColor>
#include <QDebug>
#include <QThread>
#include <QProgressDialog>
#include <QDockWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QWidget>
#include <QGridLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_dataThread(nullptr), m_dataManager(nullptr), m_plotContainer(nullptr), m_signalDock(nullptr), m_signalTree(nullptr), m_signalTreeModel(nullptr), m_progressDialog(nullptr), m_activePlot(nullptr)
{
    // 1. 设置数据管理线程 (Milestone 2)
    setupDataManagerThread();

    // 2. 创建中央绘图区 (Milestone 1 / 2.2)
    m_plotContainer = new QWidget(this);
    m_plotContainer->setLayout(new QGridLayout()); // 初始为空网格
    setCentralWidget(m_plotContainer);

    // 3. 创建动作和菜单 (顺序调整)
    createActions();
    createDocks(); // <-- 必须在 createMenus 之前调用，以便菜单能引用 m_signalDock
    createMenus(); // <-- 移到 Docks 之后

    // 4. 创建信号停靠栏 (Milestone 1 / 6)
    // createDocks(); // <-- 移动到上面

    // 5. 设置窗口标题和大小
    setWindowTitle(tr("Data Inspector (Async)"));
    resize(1280, 800); // 增大默认大小

    // 6. 设置初始布局 (2.2)
    setupPlotLayout(1, 1);

    // 7. 创建进度对话框
    m_progressDialog = new QProgressDialog(this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setAutoClose(true);
    m_progressDialog->setAutoReset(true);
    m_progressDialog->setMinimum(0);
    m_progressDialog->setMaximum(100);
    m_progressDialog->setCancelButton(nullptr); // 修复: m_progressDialog_ -> m_progressDialog
    m_progressDialog->hide();                   // <-- 新增：确保对话框初始时是隐藏的, 修复Bug
}

MainWindow::~MainWindow()
{
    // 正确停止工作线程
    if (m_dataThread)
    {
        m_dataThread->quit();
        m_dataThread->wait();
    }
    // m_dataManager 会随线程自动删除
}

void MainWindow::setupDataManagerThread()
{
    m_dataThread = new QThread(this);
    m_dataManager = new DataManager(); // 没有父对象

    // 1. 将 dataManager 移动到工作线程
    m_dataManager->moveToThread(m_dataThread);

    // 2. 连接信号槽
    //    [GUI] -> [Worker] (请求加载)
    connect(this, &MainWindow::requestLoadCsv,
            m_dataManager, &DataManager::loadCsvFile,
            Qt::QueuedConnection);

    //    [Worker] -> [GUI] (报告进度)
    connect(m_dataManager, &DataManager::loadProgress,
            this, &MainWindow::showLoadProgress,
            Qt::QueuedConnection);

    //    [Worker] -> [GUI] (报告成功)
    connect(m_dataManager, &DataManager::loadFinished,
            this, &MainWindow::onDataLoadFinished,
            Qt::QueuedConnection);

    //    [Worker] -> [GUI] (报告失败)
    connect(m_dataManager, &DataManager::loadFailed,
            this, &MainWindow::onDataLoadFailed,
            Qt::QueuedConnection);

    // 3. 线程退出时自动删除 dataManager
    connect(m_dataThread, &QThread::finished, m_dataManager, &QObject::deleteLater);

    // 4. 启动线程的事件循环
    m_dataThread->start();

    qDebug() << "Main Thread ID:" << QThread::currentThreadId();
    qDebug() << "DataManager thread started.";
}

void MainWindow::createActions()
{
    // 文件菜单
    m_loadFileAction = new QAction(tr("&Load CSV..."), this);
    m_loadFileAction->setShortcut(QKeySequence::Open);
    connect(m_loadFileAction, &QAction::triggered, this, &MainWindow::on_actionLoadFile_triggered);

    // 布局菜单
    m_layout1x1Action = new QAction(tr("1x1 Layout"), this);
    connect(m_layout1x1Action, &QAction::triggered, this, &MainWindow::on_actionLayout1x1_triggered);

    m_layout2x2Action = new QAction(tr("2x2 Layout"), this);
    connect(m_layout2x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout2x2_triggered);

    m_layout3x2Action = new QAction(tr("3x2 Layout"), this);
    connect(m_layout3x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout3x2_triggered);
}

void MainWindow::createMenus()
{
    // 文件菜单
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_loadFileAction);

    // 布局菜单
    QMenu *layoutMenu = menuBar()->addMenu(tr("&Layout"));
    layoutMenu->addAction(m_layout1x1Action);
    layoutMenu->addAction(m_layout2x2Action);
    layoutMenu->addAction(m_layout3x2Action);

    // 新增：视图菜单
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    if (m_signalDock)
    {
        // 添加一个动作来打开/关闭 "Signals" Dock
        viewMenu->addAction(m_signalDock->toggleViewAction());
    }
}

void MainWindow::createDocks()
{
    m_signalDock = new QDockWidget(tr("Signals"), this);

    m_signalTree = new QTreeView(m_signalDock);
    m_signalTreeModel = new QStandardItemModel(m_signalDock);
    m_signalTree->setModel(m_signalTreeModel);
    m_signalTree->setHeaderHidden(true);

    m_signalDock->setWidget(m_signalTree);

    // 新增：限制 Dock 功能，禁止其“浮动”成独立窗口
    // 仅允许关闭和移动
    m_signalDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);

    // 连接双击信号 (5.1 节)
    connect(m_signalTree, &QTreeView::doubleClicked, this, &MainWindow::onSignalDoubleClicked);
}

void MainWindow::setupPlotInteractions(QCustomPlot *plot)
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    plot->legend->setVisible(true);

    // 连接点击信号，用于设置 "active" plot (5.1 节)
    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);
}

// --- 绘图布局 ---

void MainWindow::clearPlotLayout()
{
    // 从布局中移除所有 widget
    QLayout *layout = m_plotContainer->layout();
    if (layout)
    {
        QLayoutItem *item;
        while ((item = layout->takeAt(0)) != nullptr)
        {
            if (item->widget())
            {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
    m_plotWidgets.clear();
    m_activePlot = nullptr;
}

void MainWindow::setupPlotLayout(int rows, int cols)
{
    clearPlotLayout(); // 清理旧布局

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    if (!grid)
    {
        grid = new QGridLayout(m_plotContainer);
    }

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            QCustomPlot *plot = new QCustomPlot(m_plotContainer);
            setupPlotInteractions(plot);

            grid->addWidget(plot, r, c);
            m_plotWidgets.append(plot);
        }
    }

    // 默认激活第一个 plot
    if (!m_plotWidgets.isEmpty())
    {
        m_activePlot = m_plotWidgets.first();
    }
}

void MainWindow::on_actionLayout1x1_triggered()
{
    setupPlotLayout(1, 1);
}

void MainWindow::on_actionLayout2x2_triggered()
{
    setupPlotLayout(2, 2);
}

void MainWindow::on_actionLayout3x2_triggered()
{
    setupPlotLayout(3, 2);
}

// --- 槽函数 ---

void MainWindow::on_actionLoadFile_triggered()
{
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Open CSV File"), "", tr("CSV Files (*.csv *.txt)"));

    if (filePath.isEmpty())
    {
        return; // 用户取消
    }

    // 重置进度条并显示
    m_progressDialog->setValue(0);
    m_progressDialog->setLabelText(tr("Loading %1...").arg(filePath));
    m_progressDialog->show();

    // 发送异步加载请求
    emit requestLoadCsv(filePath);
}

void MainWindow::showLoadProgress(int percentage)
{
    m_progressDialog->setValue(percentage);
}

void MainWindow::onDataLoadFinished(const CsvData &data)
{
    m_progressDialog->hide();
    qDebug() << "Main Thread: Load finished. Received" << data.timeData.count() << "data points.";

    // 1. 缓存数据
    m_loadedTimeData = data.timeData;
    m_loadedValueData = data.valueData;

    // 2. 清理所有图表
    for (QCustomPlot *plot : m_plotWidgets)
    {
        plot->clearGraphs();
        plot->replot();
    }

    // 3. 填充信号树
    populateSignalTree(data);

    QMessageBox::information(this, tr("Success"), tr("Successfully loaded %1 data points.").arg(data.timeData.count()));
}

void MainWindow::populateSignalTree(const CsvData &data)
{
    m_signalTreeModel->clear();

    // headers[0] 是 Time, 我们跳过它
    for (int i = 1; i < data.headers.count(); ++i)
    {
        QString signalName = data.headers[i].trimmed();
        if (signalName.isEmpty())
        {
            signalName = tr("Signal %1").arg(i);
        }

        QStandardItem *item = new QStandardItem(signalName);
        item->setEditable(false);

        // 关键：将此信号在 m_loadedValueData 中的索引存储在 UserRole 中
        // (i-1) 是因为它在 valueData 向量中的索引
        item->setData(i - 1, Qt::UserRole);

        m_signalTreeModel->appendRow(item);
    }
}

void MainWindow::onDataLoadFailed(const QString &errorString)
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), errorString);
}

void MainWindow::onPlotClicked()
{
    // 1. 获取发送信号的 plot
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot)
        return;

    // 2. 将其设为 "active"
    m_activePlot = clickedPlot;

    // 3. (可选) 添加视觉反馈
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot == m_activePlot)
        {
            // 设置活动边框 (例如，蓝色)
            plot->setStyleSheet("border: 2px solid #0078d4;");
        }
        else
        {
            // 恢复默认边框
            plot->setStyleSheet("");
        }
    }
    qDebug() << "Active plot set to:" << m_activePlot;
}

void MainWindow::onSignalDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    // 修复: 在尝试添加信号之前，首先检查数据是否已加载
    if (m_loadedTimeData.isEmpty())
    {
        QMessageBox::information(this, tr("No Data Loaded"),
                                 tr("Please load a data file before adding signals."));
        return;
    }

    // 1. 检查是否有活动图表 (5.1 节)
    if (!m_activePlot)
    {
        QMessageBox::information(this, tr("No Plot Selected"), tr("Please click on a plot to activate it before adding a signal."));
        return;
    }

    // 2. 获取信号信息
    int signalIndex = index.data(Qt::UserRole).toInt();
    QString signalName = index.data(Qt::DisplayRole).toString();

    if (signalIndex < 0 || signalIndex >= m_loadedValueData.count())
    {
        qWarning() << "Invalid signal index" << signalIndex;
        return;
    }

    // 移除多余的检查，因为我们在函数顶部已经检查过了
    /*
    if (m_loadedTimeData.isEmpty()) {
        qWarning() << "No time data loaded.";
        return;
    }
    */

    qDebug() << "Adding signal" << signalName << "(index" << signalIndex << ") to plot" << m_activePlot;

    // 3. 添加图表到活动 plot
    QCPGraph *graph = m_activePlot->addGraph();
    graph->setName(signalName);

    // 4. 设置数据
    graph->setData(m_loadedTimeData, m_loadedValueData[signalIndex]);

    // 随机一种清晰的颜色
    QColor color(10 + (qrand() % 245), 10 + (qrand() % 245), 10 + (qrand() % 245));
    graph->setPen(QPen(color));

    // 5. 缩放并重绘
    m_activePlot->rescaleAxes();
    m_activePlot->replot();
}