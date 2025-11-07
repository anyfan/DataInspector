#include "mainwindow.h"
#include "qcustomplot.h"
#include "signaltreedelegate.h" // <-- 包含自定义委托

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
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QColorDialog> // <-- 用于颜色选择
#include <QFrame>
#include <QVBoxLayout>
#include <QToolBar>       // <-- 新增
#include <QActionGroup>   // <-- 新增
#include <QTimer>         // <-- 新增
#include <QPushButton>    // <-- 新增
#include <QSlider>        // <-- 新增
#include <QLabel>         // <-- 新增
#include <QDoubleSpinBox> // <-- 新增
#include <QHBoxLayout>    // <-- 新增
#include <QStyle>         // <-- 新增 (用于标准图标)

// 定义一个自定义角色 (Qt::UserRole + 1) 来存储 QPen
const int PenDataRole = Qt::UserRole + 1;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_dataThread(nullptr), m_dataManager(nullptr), m_plotContainer(nullptr), m_signalDock(nullptr), m_signalTree(nullptr), m_signalTreeModel(nullptr), m_progressDialog(nullptr), m_activePlot(nullptr), m_lastMousePlot(nullptr), m_cursorMode(NoCursor), m_cursorKey1(0), m_cursorKey2(0)
{
    // 1. 设置数据管理线程
    setupDataManagerThread();

    // 2. 创建中央绘图区
    m_plotContainer = new QWidget(this);
    m_plotContainer->setLayout(new QGridLayout()); // 初始为空网格
    setCentralWidget(m_plotContainer);

    // 3. 创建动作和菜单 (顺序调整)
    createActions();
    createDocks();
    createToolBars();   // <-- 新增
    createReplayDock(); // <-- 新增
    createMenus();      // <-- 移到 Docks 之后

    // 4. 设置窗口标题和大小
    setWindowTitle(tr("Data Inspector (Async)"));
    resize(1280, 800);

    // 5. 设置初始布局
    setupPlotLayout(1, 1);

    // 6. 创建进度对话框
    m_progressDialog = new QProgressDialog(this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setAutoClose(true);
    m_progressDialog->setAutoReset(true);
    m_progressDialog->setMinimum(0);
    m_progressDialog->setMaximum(100);
    m_progressDialog->setCancelButton(nullptr);
    m_progressDialog->hide();

    // 7. 注册 QPen 类型
    qRegisterMetaType<QPen>("QPen");

    // 8. 初始化重放定时器
    m_replayTimer = new QTimer(this);
    connect(m_replayTimer, &QTimer::timeout, this, &MainWindow::onReplayTimerTimeout);
}

MainWindow::~MainWindow()
{
    // 正确停止工作线程
    if (m_dataThread)
    {
        m_dataThread->quit();
        m_dataThread->wait();
    }
}

void MainWindow::setupDataManagerThread()
{
    // ... (现有代码) ...
    m_dataThread = new QThread(this);
    m_dataManager = new DataManager(); // 没有父对象
    m_dataManager->moveToThread(m_dataThread);

    connect(this, &MainWindow::requestLoadCsv, m_dataManager, &DataManager::loadCsvFile, Qt::QueuedConnection);
    connect(m_dataManager, &DataManager::loadProgress, this, &MainWindow::showLoadProgress, Qt::QueuedConnection);
    connect(m_dataManager, &DataManager::loadFinished, this, &MainWindow::onDataLoadFinished, Qt::QueuedConnection);
    connect(m_dataManager, &DataManager::loadFailed, this, &MainWindow::onDataLoadFailed, Qt::QueuedConnection);
    connect(m_dataThread, &QThread::finished, m_dataManager, &QObject::deleteLater);

    m_dataThread->start();

    qDebug() << "Main Thread ID:" << QThread::currentThreadId();
    qDebug() << "DataManager thread started.";
}

void MainWindow::createActions()
{
    // ... (现有代码) ...
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

    // --- 新增：视图/游标动作 ---
    m_cursorNoneAction = new QAction(tr("No Cursor"), this);
    m_cursorNoneAction->setCheckable(true);
    m_cursorNoneAction->setChecked(true);

    m_cursorSingleAction = new QAction(tr("Single Cursor"), this);
    m_cursorSingleAction->setCheckable(true);

    m_cursorDoubleAction = new QAction(tr("Double Cursor"), this);
    m_cursorDoubleAction->setCheckable(true);

    m_cursorGroup = new QActionGroup(this);
    m_cursorGroup->addAction(m_cursorNoneAction);
    m_cursorGroup->addAction(m_cursorSingleAction);
    m_cursorGroup->addAction(m_cursorDoubleAction);
    connect(m_cursorGroup, &QActionGroup::triggered, this, &MainWindow::onCursorModeChanged);

    m_replayAction = new QAction(tr("Replay"), this);
    m_replayAction->setCheckable(true);
    connect(m_replayAction, &QAction::toggled, this, &MainWindow::onReplayActionToggled);
}

void MainWindow::createMenus()
{
    // ... (现有代码) ...
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_loadFileAction);

    QMenu *layoutMenu = menuBar()->addMenu(tr("&Layout"));
    layoutMenu->addAction(m_layout1x1Action);
    layoutMenu->addAction(m_layout2x2Action);
    layoutMenu->addAction(m_layout3x2Action);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    if (m_signalDock)
    {
        viewMenu->addAction(m_signalDock->toggleViewAction());
    }
    // --- 新增：重放面板菜单项 ---
    if (m_replayDock)
    {
        viewMenu->addAction(m_replayDock->toggleViewAction());
    }
}

/**
 * @brief [新增] 创建视图工具栏 (用于游标和重放)
 */
void MainWindow::createToolBars()
{
    m_viewToolBar = new QToolBar(tr("View Toolbar"), this);

    // “弹簧”小部件，将所有动作推到右侧
    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_viewToolBar->addWidget(spacer);

    // 添加游标动作
    m_viewToolBar->addAction(m_cursorNoneAction);
    m_viewToolBar->addAction(m_cursorSingleAction);
    m_viewToolBar->addAction(m_cursorDoubleAction);
    m_viewToolBar->addSeparator();
    m_viewToolBar->addAction(m_replayAction);

    addToolBar(Qt::TopToolBarArea, m_viewToolBar);
}

void MainWindow::createDocks()
{
    // ... (现有代码) ...
    m_signalDock = new QDockWidget(tr("Signals"), this);
    m_signalTree = new QTreeView(m_signalDock);
    m_signalTreeModel = new QStandardItemModel(m_signalDock);
    m_signalTree->setModel(m_signalTreeModel);
    m_signalTree->setHeaderHidden(true);
    m_signalTree->setItemDelegate(new SignalTreeDelegate(m_signalTree));
    m_signalDock->setWidget(m_signalTree);
    m_signalDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);

    connect(m_signalTreeModel, &QStandardItemModel::itemChanged, this, &MainWindow::onSignalItemChanged);
    connect(m_signalTree, &QTreeView::doubleClicked, this, &MainWindow::onSignalItemDoubleClicked);
}

/**
 * @brief [新增] 创建底部重放停靠栏
 */
void MainWindow::createReplayDock()
{
    m_replayDock = new QDockWidget(tr("Replay Controls"), this);
    m_replayWidget = new QWidget(m_replayDock);

    QHBoxLayout *layout = new QHBoxLayout(m_replayWidget);

    m_stepBackwardButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaSeekBackward), "", m_replayWidget);
    m_playPauseButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "", m_replayWidget);
    m_stepForwardButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaSeekForward), "", m_replayWidget);

    m_currentTimeLabel = new QLabel(tr("Time: 0.0"), m_replayWidget);
    m_timeSlider = new QSlider(Qt::Horizontal, m_replayWidget);
    m_timeSlider->setMinimum(0);
    m_timeSlider->setMaximum(10000); // 10000 步的分辨率

    QLabel *speedLabel = new QLabel(tr("Speed:"), m_replayWidget);
    m_speedSpinBox = new QDoubleSpinBox(m_replayWidget);
    m_speedSpinBox->setMinimum(0.1);
    m_speedSpinBox->setMaximum(100.0);
    m_speedSpinBox->setValue(1.0);
    m_speedSpinBox->setSuffix("x");

    layout->addWidget(m_stepBackwardButton);
    layout->addWidget(m_playPauseButton);
    layout->addWidget(m_stepForwardButton);
    layout->addWidget(m_currentTimeLabel);
    layout->addWidget(m_timeSlider, 1); // 1 = stretch factor
    layout->addWidget(speedLabel);
    layout->addWidget(m_speedSpinBox);

    m_replayDock->setWidget(m_replayWidget);
    m_replayDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::BottomDockWidgetArea, m_replayDock);

    // 默认隐藏
    m_replayDock->hide();

    // 连接信号
    connect(m_playPauseButton, &QPushButton::clicked, this, &MainWindow::onPlayPauseClicked);
    connect(m_stepForwardButton, &QPushButton::clicked, this, &MainWindow::onStepForwardClicked);
    connect(m_stepBackwardButton, &QPushButton::clicked, this, &MainWindow::onStepBackwardClicked);
    connect(m_timeSlider, &QSlider::valueChanged, this, &MainWindow::onTimeSliderChanged);
}

void MainWindow::setupPlotInteractions(QCustomPlot *plot)
{
    // ... (现有代码) ...
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    plot->legend->setVisible(true);

    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);
    // --- 新增：连接鼠标移动信号 ---
    connect(plot, &QCustomPlot::mouseMove, this, &MainWindow::onPlotMouseMove);
}

// --- 绘图布局 ---

void MainWindow::clearPlotLayout()
{
    // --- 新增：在删除 plot 之前必须清除游标 ---
    // 否则游标项 (QCPItemLine 等) 会持有悬空指针
    clearCursors();
    // ------------------------------------

    // ... (现有代码) ...
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
    m_plotFrameMap.clear();
    m_plotGraphMap.clear();    // (Bug 2 修复)
    m_lastMousePlot = nullptr; // <-- 新增
}

void MainWindow::setupPlotLayout(int rows, int cols)
{
    clearPlotLayout(); // 清理旧布局

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    // ... (现有代码) ...
    if (!grid)
    {
        grid = new QGridLayout(m_plotContainer);
    }

    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            QFrame *plotFrame = new QFrame(m_plotContainer);
            plotFrame->setFrameShape(QFrame::NoFrame);
            plotFrame->setLineWidth(2);
            plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

            QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
            frameLayout->setContentsMargins(0, 0, 0, 0);

            QCustomPlot *plot = new QCustomPlot(plotFrame);
            setupPlotInteractions(plot); // <-- setupPlotInteractions 在此调用

            frameLayout->addWidget(plot);
            grid->addWidget(plotFrame, r, c);
            m_plotWidgets.append(plot);
            m_plotFrameMap.insert(plot, plotFrame);
        }
    }

    if (!m_plotWidgets.isEmpty())
    {
        m_activePlot = m_plotWidgets.first();
        m_lastMousePlot = m_activePlot; // <-- 新增
        QFrame *frame = m_plotFrameMap.value(m_activePlot);
        if (frame)
        {
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        }
        updateSignalTreeChecks();
        m_signalTree->viewport()->update();
    }

    // --- 新增：布局更改后，重建游标 ---
    setupCursors();
    // --------------------------------
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
    // ... (现有代码) ...
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Open CSV File"), "", tr("CSV Files (*.csv *.txt)"));
    if (filePath.isEmpty())
        return;

    m_progressDialog->setValue(0);
    m_progressDialog->setLabelText(tr("Loading %1...").arg(filePath));
    m_progressDialog->show();

    emit requestLoadCsv(filePath);
}

void MainWindow::showLoadProgress(int percentage)
{
    // ... (现有代码) ...
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
    m_plotGraphMap.clear();

    // 3. 填充信号树
    {
        QSignalBlocker blocker(m_signalTreeModel);
        populateSignalTree(data);
    }
    m_signalTree->reset(); // (Bug 1 修复)

    // 4. 更新勾选状态
    updateSignalTreeChecks();

    QMessageBox::information(this, tr("Success"), tr("Successfully loaded %1 data points.").arg(data.timeData.count()));

    // --- 新增：数据加载后，更新重放控件和游标 ---
    if (!m_loadedTimeData.isEmpty())
    {
        // 将游标 1 移动到数据起点
        m_cursorKey1 = m_loadedTimeData.first();
        m_cursorKey2 = m_loadedTimeData.first() + getGlobalTimeRange().size() * 0.1; // 游标 2 在 10% 处
        updateCursors(m_cursorKey1, 1);
        updateCursors(m_cursorKey2, 2);
    }
    updateReplayControls(); // 设置滑块范围
    // -----------------------------------------
}

void MainWindow::populateSignalTree(const CsvData &data)
{
    // ... (现有代码) ...
    m_signalTreeModel->clear();
    m_signalPens.clear();

    for (int i = 1; i < data.headers.count(); ++i)
    {
        QString signalName = data.headers[i].trimmed();
        if (signalName.isEmpty())
            signalName = tr("Signal %1").arg(i);

        QStandardItem *item = new QStandardItem(signalName);
        item->setEditable(false);
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);
        item->setData(i - 1, Qt::UserRole);

        QColor color(10 + (qrand() % 245), 10 + (qrand() % 245), 10 + (qrand() % 245));
        QPen pen(color, 1);
        m_signalPens.append(pen);
        item->setData(QVariant::fromValue(pen), PenDataRole);

        m_signalTreeModel->appendRow(item);
    }
}

void MainWindow::onDataLoadFailed(const QString &errorString)
{
    // ... (现有代码) ...
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), errorString);
}

void MainWindow::onPlotClicked()
{
    // ... (现有代码) ...
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot || clickedPlot == m_activePlot)
        return;

    m_activePlot = clickedPlot;
    m_lastMousePlot = clickedPlot; // <-- 新增

    for (QCustomPlot *plot : m_plotWidgets)
    {
        QFrame *frame = m_plotFrameMap.value(plot);
        if (!frame)
            continue;
        if (plot == m_activePlot)
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        else
            frame->setStyleSheet("QFrame { border: 2px solid transparent; }");
    }
    qDebug() << "Active plot set to:" << m_activePlot;
    updateSignalTreeChecks();
    m_signalTree->viewport()->update();
}

void MainWindow::updateSignalTreeChecks()
{
    // ... (现有代码) ...
    QSignalBlocker blocker(m_signalTreeModel);
    const auto &activeGraphs = m_plotGraphMap.value(m_activePlot);
    for (int i = 0; i < m_signalTreeModel->rowCount(); ++i)
    {
        QStandardItem *item = m_signalTreeModel->item(i);
        if (!item)
            continue;
        int signalIndex = item->data(Qt::UserRole).toInt();
        if (activeGraphs.contains(signalIndex))
            item->setCheckState(Qt::Checked);
        else
            item->setCheckState(Qt::Unchecked);
    }
}

void MainWindow::onSignalItemChanged(QStandardItem *item)
{
    if (!item)
        return;
    if (m_loadedTimeData.isEmpty())
    { /* ... (现有代码) ... */
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
        }
        return;
    }
    if (!m_activePlot)
    { /* ... (现有代码) ... */
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
            QMessageBox::information(this, tr("No Plot Selected"), tr("Please click on a plot to activate it before adding a signal."));
        }
        return;
    }

    int signalIndex = item->data(Qt::UserRole).toInt();
    QString signalName = item->text();
    if (signalIndex < 0 || signalIndex >= m_loadedValueData.count())
    { /* ... (现有代码) ... */
        qWarning() << "Invalid signal index" << signalIndex;
        return;
    }

    QSignalBlocker blocker(m_signalTreeModel);
    if (item->checkState() == Qt::Checked)
    {
        if (m_plotGraphMap.value(m_activePlot).contains(signalIndex))
        { /* ... (现有代码) ... */
            qWarning() << "Graph already exists on this plot.";
            return;
        }
        qDebug() << "Adding signal" << signalName << "(index" << signalIndex << ") to plot" << m_activePlot;
        QCPGraph *graph = m_activePlot->addGraph();
        graph->setName(signalName);
        graph->setData(m_loadedTimeData, m_loadedValueData[signalIndex]);
        QPen pen = item->data(PenDataRole).value<QPen>();
        graph->setPen(pen);
        m_plotGraphMap[m_activePlot].insert(signalIndex, graph);
        m_activePlot->rescaleAxes();
        m_activePlot->replot();
    }
    else // Qt::Unchecked
    {
        if (!m_plotGraphMap.value(m_activePlot).contains(signalIndex))
        { /* ... (现有代码) ... */
            return;
        }
        QCPGraph *graph = m_plotGraphMap.value(m_activePlot).value(signalIndex);
        if (graph)
        { /* ... (现有代码) ... */
            qDebug() << "Removing signal" << signalName << "from plot" << m_activePlot;
            m_activePlot->removeGraph(graph);
            m_plotGraphMap[m_activePlot].remove(signalIndex);
            m_activePlot->replot();
        }
    }

    // --- 新增：添加/删除 graph 后，重建游标以包含/排除它 ---
    setupCursors();
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
    // ---------------------------------------------------
}

void MainWindow::onSignalItemDoubleClicked(const QModelIndex &index)
{
    // ... (现有代码) ...
    if (!index.isValid())
        return;
    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);
    if (!item)
        return;
    int signalIndex = item->data(Qt::UserRole).toInt();
    if (signalIndex < 0 || signalIndex >= m_signalPens.count())
        return;

    QPen currentPen = item->data(PenDataRole).value<QPen>();
    QColor newColor = QColorDialog::getColor(currentPen.color(), this, tr("Select Signal Color"));
    if (!newColor.isValid())
        return;

    QPen newPen = currentPen;
    newPen.setColor(newColor);
    item->setData(QVariant::fromValue(newPen), PenDataRole);
    m_signalPens[signalIndex] = newPen;

    for (auto it = m_plotGraphMap.begin(); it != m_plotGraphMap.end(); ++it)
    {
        if (it.value().contains(signalIndex))
        {
            QCPGraph *graph = it.value().value(signalIndex);
            if (graph)
            {
                graph->setPen(newPen);
                graph->parentPlot()->replot();
            }
        }
    }
}

// ===============================================
// === 新增：游标和重放功能实现 ===
// ===============================================

/**
 * @brief [新增] 响应游标模式切换
 */
void MainWindow::onCursorModeChanged(QAction *action)
{
    if (action == m_cursorNoneAction)
    {
        m_cursorMode = NoCursor;
        // 恢复平移
        for (QCustomPlot *plot : m_plotWidgets)
        {
            plot->setInteraction(QCP::iRangeDrag, true);
        }
    }
    else
    {
        if (action == m_cursorSingleAction)
            m_cursorMode = SingleCursor;
        else if (action == m_cursorDoubleAction)
            m_cursorMode = DoubleCursor;

        // 禁用平移，避免与游标拖动冲突
        for (QCustomPlot *plot : m_plotWidgets)
        {
            plot->setInteraction(QCP::iRangeDrag, false);
        }
    }

    // 重建游标 UI
    setupCursors();
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
}

/**
 * @brief [新增] 响应重放按钮切换
 */
void MainWindow::onReplayActionToggled(bool checked)
{
    m_replayDock->setVisible(checked);

    if (checked && m_cursorMode == NoCursor)
    {
        // 如果没有游标，自动启用单游标
        m_cursorSingleAction->setChecked(true);
        onCursorModeChanged(m_cursorSingleAction); // 手动触发更新
    }
}

/**
 * @brief [新增] 响应 Plot 上的鼠标移动
 */
void MainWindow::onPlotMouseMove(QMouseEvent *event)
{
    if (m_cursorMode == NoCursor)
        return; // 游标未激活

    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (plot)
        m_lastMousePlot = plot; // 记录最后交互的 plot

    // 从 plot 获取 x 坐标
    double key = plot->xAxis->pixelToCoord(event->pos().x());

    // 左键拖动 -> 移动游标 1
    if (event->buttons() & Qt::LeftButton)
    {
        updateCursors(key, 1);
    }
    // 右键拖动 (仅在双游标模式) -> 移动游标 2
    else if ((event->buttons() & Qt::RightButton) && m_cursorMode == DoubleCursor)
    {
        updateCursors(key, 2);
    }
}

/**
 * @brief [新增] 清除所有游标图元
 */
void MainWindow::clearCursors()
{
    qDeleteAll(m_cursorLines1);
    m_cursorLines1.clear();
    qDeleteAll(m_cursorLines2);
    m_cursorLines2.clear();

    qDeleteAll(m_cursorLabels1);
    m_cursorLabels1.clear();
    qDeleteAll(m_cursorLabels2);
    m_cursorLabels2.clear();

    // QMap 的 value 是指针，需要特殊删除
    qDeleteAll(m_graphTracers1);
    m_graphTracers1.clear();
    qDeleteAll(m_graphTracers2);
    m_graphTracers2.clear();
}

/**
 * @brief [新增] 根据 m_cursorMode 设置游标
 */
void MainWindow::setupCursors()
{
    clearCursors();

    if (m_cursorMode == NoCursor)
    {
        // 隐藏所有图表的游标信息
        for (QCustomPlot *plot : m_plotWidgets)
        {
            plot->replot();
        }
        return;
    }

    QPen pen1(Qt::red, 0, Qt::DashLine);
    QPen pen2(Qt::blue, 0, Qt::DashLine);

    // 1. 为每个 Plot 创建垂直线
    for (QCustomPlot *plot : m_plotWidgets)
    {
        // 创建游标 1
        QCPItemLine *line1 = new QCPItemLine(plot);
        line1->setPen(pen1);
        m_cursorLines1.append(line1);

        QCPItemText *label1 = new QCPItemText(plot);
        label1->setLayer("overlay");
        label1->setClipToAxisRect(false);
        label1->setPadding(QMargins(5, 5, 5, 5));
        label1->setBrush(QBrush(QColor(255, 255, 255, 200)));
        label1->setPen(QPen(Qt::black));
        label1->setPositionAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_cursorLabels1.append(label1);

        if (m_cursorMode == DoubleCursor)
        {
            // 创建游标 2
            QCPItemLine *line2 = new QCPItemLine(plot);
            line2->setPen(pen2);
            m_cursorLines2.append(line2);

            QCPItemText *label2 = new QCPItemText(plot);
            label2->setLayer("overlay");
            label2->setClipToAxisRect(false);
            label2->setPadding(QMargins(5, 5, 5, 5));
            label2->setBrush(QBrush(QColor(255, 255, 255, 200)));
            label2->setPen(QPen(Qt::black));
            label2->setPositionAlignment(Qt::AlignTop | Qt::AlignRight);
            m_cursorLabels2.append(label2);
        }
    }

    // 2. 为每个 Graph 创建跟踪器
    for (auto it = m_plotGraphMap.begin(); it != m_plotGraphMap.end(); ++it)
    {
        for (QCPGraph *graph : it.value())
        {
            // 创建游标 1 的跟踪器
            QCPItemTracer *tracer1 = new QCPItemTracer(graph->parentPlot());
            tracer1->setGraph(graph);
            tracer1->setInterpolating(true);
            tracer1->setVisible(true);
            m_graphTracers1.insert(graph, tracer1);

            if (m_cursorMode == DoubleCursor)
            {
                // 创建游标 2 的跟踪器
                QCPItemTracer *tracer2 = new QCPItemTracer(graph->parentPlot());
                tracer2->setGraph(graph);
                tracer2->setInterpolating(true);
                tracer2->setVisible(true);
                m_graphTracers2.insert(graph, tracer2);
            }
        }
    }
}

/**
 * @brief [新增] 核心同步逻辑：更新所有游标
 */
void MainWindow::updateCursors(double key, int cursorIndex)
{
    if (m_cursorMode == NoCursor)
        return;

    // 1. 钳制 key 在数据范围内
    QCPRange range = getGlobalTimeRange();
    if (key < range.lower)
        key = range.lower;
    if (key > range.upper)
        key = range.upper;

    // 2. 存储 key
    QList<QCPItemLine *> *lines;
    QMap<QCPGraph *, QCPItemTracer *> *tracers;
    QList<QCPItemText *> *labels;
    QColor penColor;

    if (cursorIndex == 1)
    {
        m_cursorKey1 = key;
        lines = &m_cursorLines1;
        tracers = &m_graphTracers1;
        labels = &m_cursorLabels1;
        penColor = Qt::red;
    }
    else if (cursorIndex == 2 && m_cursorMode == DoubleCursor)
    {
        m_cursorKey2 = key;
        lines = &m_cursorLines2;
        tracers = &m_graphTracers2;
        labels = &m_cursorLabels2;
        penColor = Qt::blue;
    }
    else
    {
        return; // 无效
    }

    // 3. 遍历所有 plot，更新它们的游标
    for (int i = 0; i < m_plotWidgets.size(); ++i)
    {
        QCustomPlot *plot = m_plotWidgets.at(i);
        if (i >= lines->size())
            continue; // 安全检查

        // A. 更新垂直线
        QCPItemLine *line = lines->at(i);
        line->start->setCoords(key, plot->yAxis->range().lower);
        line->end->setCoords(key, plot->yAxis->range().maxRange);

        // B. 更新文本标签
        QString labelText = QString("<font color='%1'><b>T: %2</b></font><br>").arg(penColor.name()).arg(key, 0, 'f', 4);
        int graphCount = 0;

        // 遍历此 plot 上的所有 graph
        if (m_plotGraphMap.contains(plot))
        {
            for (QCPGraph *graph : m_plotGraphMap.value(plot))
            {
                if (tracers->contains(graph))
                {
                    QCPItemTracer *tracer = tracers->value(graph);
                    tracer->setGraphKey(key); // 自动插值
                    double value = tracer->position->value();

                    labelText += QString("%1: %2<br>")
                                     .arg(graph->name())
                                     .arg(value, 0, 'f', 3);
                    graphCount++;
                }
            }
        }

        QCPItemText *label = labels->at(i);
        label->setText(labelText);
        label->position->setCoords(key, plot->yAxis->range().upper);

        // C. 如果有双光标，计算差值
        if (m_cursorMode == DoubleCursor && graphCount > 0)
        {
            QString deltaText = QString("<font color='purple'><b>ΔT: %1</b></font><br>").arg(qAbs(m_cursorKey1 - m_cursorKey2), 0, 'f', 4);
            QMap<QCPGraph *, QCPItemTracer *> *tracers1 = &m_graphTracers1;
            QMap<QCPGraph *, QCPItemTracer *> *tracers2 = &m_graphTracers2;

            if (m_plotGraphMap.contains(plot))
            {
                for (QCPGraph *graph : m_plotGraphMap.value(plot))
                {
                    if (tracers1->contains(graph) && tracers2->contains(graph))
                    {
                        double val1 = tracers1->value(graph)->position->value();
                        double val2 = tracers2->value(graph)->position->value();
                        deltaText += QString("Δ%1: %2<br>")
                                         .arg(graph->name())
                                         .arg(qAbs(val1 - val2), 0, 'f', 3);
                    }
                }
            }
            // 将 delta 信息附加到活动 plot 的标签 1 上
            if (plot == m_activePlot)
            {
                labels->at(i)->setText(labels->at(i)->text() + "<br>" + deltaText);
            }
        }

        plot->replot();
    }

    // 4. 更新重放控件 (仅当游标 1 移动时)
    if (cursorIndex == 1)
    {
        updateReplayControls();
    }
}

/**
 * @brief [新增] 更新重放控件 (滑块和标签)
 */
void MainWindow::updateReplayControls()
{
    if (m_loadedTimeData.isEmpty())
        return;

    QCPRange range = getGlobalTimeRange();
    if (range.size() <= 0)
        return;

    // 更新标签
    m_currentTimeLabel->setText(tr("Time: %1").arg(m_cursorKey1, 0, 'f', 4));

    // 更新滑块
    double relativePos = (m_cursorKey1 - range.lower) / range.size();
    {
        QSignalBlocker blocker(m_timeSlider); // 阻止触发 onTimeSliderChanged
        m_timeSlider->setValue(relativePos * m_timeSlider->maximum());
    }
}

/**
 * @brief [新增] 获取全局时间范围
 */
QCPRange MainWindow::getGlobalTimeRange() const
{
    if (m_loadedTimeData.isEmpty())
        return QCPRange(0, 1);

    return QCPRange(m_loadedTimeData.first(), m_loadedTimeData.last());
}

/**
 * @brief [新增] 估算数据时间步
 */
double MainWindow::findDataTimeStep() const
{
    if (m_loadedTimeData.size() < 2)
        return 0.01; // 默认步长

    // 假设时间步长基本恒定
    return m_loadedTimeData.at(1) - m_loadedTimeData.at(0);
}

/**
 * @brief [新增] 播放/暂停按钮点击
 */
void MainWindow::onPlayPauseClicked()
{
    if (m_replayTimer->isActive())
    {
        m_replayTimer->stop();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    }
    else
    {
        double speed = m_speedSpinBox->value();
        double timeStep = findDataTimeStep();
        if (timeStep <= 0 || speed <= 0)
            return;

        // (时间步长 / 速度) = 真实时间
        int intervalMs = (timeStep * 1000.0) / speed;
        m_replayTimer->setInterval(qMax(16, intervalMs)); // 限制最快 ~60 fps
        m_replayTimer->start();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    }
}

/**
 * @brief [新增] 重放定时器触发
 */
void MainWindow::onReplayTimerTimeout()
{
    double speed = m_speedSpinBox->value();
    double timeStep = findDataTimeStep();
    if (timeStep <= 0 || speed <= 0)
        return;

    double newKey = m_cursorKey1 + timeStep; // 总是步进
    QCPRange range = getGlobalTimeRange();

    if (newKey > range.upper)
    {
        newKey = range.lower; // 循环
    }

    // 更新游标 1
    updateCursors(newKey, 1);

    // 检查速度是否已更改，并更新间隔
    int intervalMs = (timeStep * 1000.0) / speed;
    m_replayTimer->setInterval(qMax(16, intervalMs));
}

/**
 * @brief [新增] 步进按钮
 */
void MainWindow::onStepForwardClicked()
{
    if (m_replayTimer->isActive())
        return;

    double timeStep = findDataTimeStep();
    double newKey = m_cursorKey1 + timeStep;
    updateCursors(newKey, 1); // updateCursors 会自动钳制
}

/**
 * @brief [新增] 步退按钮
 */
void MainWindow::onStepBackwardClicked()
{
    if (m_replayTimer->isActive())
        return;

    double timeStep = findDataTimeStep();
    double newKey = m_cursorKey1 - timeStep;
    updateCursors(newKey, 1); // updateCursors 会自动钳制
}

/**
 * @brief [新增] 时间滑块被用户拖动
 */
void MainWindow::onTimeSliderChanged(int value)
{
    if (m_replayTimer->isActive())
        return; // 播放时，定时器优先

    QCPRange range = getGlobalTimeRange();
    if (range.size() <= 0)
        return;

    double relativePos = (double)value / m_timeSlider->maximum();
    double newKey = range.lower + relativePos * range.size();

    updateCursors(newKey, 1);
}