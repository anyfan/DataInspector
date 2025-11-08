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
#include <QColorDialog>
#include <QFrame>
#include <QVBoxLayout>
#include <QToolBar>
#include <QActionGroup>
#include <QTimer>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QStyle>
#include <QIcon>

// 定义一个自定义角色 (Qt::UserRole + 1) 来存储 QPen
const int PenDataRole = Qt::UserRole + 1;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_dataThread(nullptr), m_dataManager(nullptr), m_plotContainer(nullptr), m_signalDock(nullptr), m_signalTree(nullptr), m_signalTreeModel(nullptr), m_progressDialog(nullptr), m_activePlot(nullptr), m_lastMousePlot(nullptr), m_cursorMode(NoCursor), m_cursorKey1(0), m_cursorKey2(0),
      // --- 新增：初始化拖拽标志 ---
      m_isDraggingCursor1(false),
      m_isDraggingCursor2(false)
// --- -------------------- ---
{
    setupDataManagerThread();

    m_plotContainer = new QWidget(this);
    m_plotContainer->setLayout(new QGridLayout());
    setCentralWidget(m_plotContainer);

    createActions();
    createDocks();
    createToolBars();
    createReplayDock();
    createMenus();

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
    m_dataThread = new QThread(this);
    m_dataManager = new DataManager();
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

    // --- 修正：视图缩放动作 ---
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon)); // 使用标准图标
    m_fitViewAction->setToolTip(tr("Fit all axes to data"));
    connect(m_fitViewAction, &QAction::triggered, this, &MainWindow::on_actionFitView_triggered);

    m_fitViewTimeAction = new QAction(tr("Fit View (Time)"), this);
    m_fitViewTimeAction->setIcon(QIcon::fromTheme("zoom-fit-width", style()->standardIcon(QStyle::SP_ArrowRight))); // 尝试主题图标
    m_fitViewTimeAction->setToolTip(tr("Fit time (X) axis to data"));
    connect(m_fitViewTimeAction, &QAction::triggered, this, &MainWindow::on_actionFitViewTime_triggered);

    m_fitViewYAction = new QAction(tr("Fit View (Y-Axis)"), this);
    m_fitViewYAction->setIcon(QIcon::fromTheme("zoom-fit-height", style()->standardIcon(QStyle::SP_ArrowDown))); // 尝试主题图标
    m_fitViewYAction->setToolTip(tr("Fit Y axis of active plot to data"));
    connect(m_fitViewYAction, &QAction::triggered, this, &MainWindow::on_actionFitViewY_triggered);
    // --- -------------------- ---

    // 视图/游标动作
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
    // 重放面板菜单项
    if (m_replayDock)
    {
        viewMenu->addAction(m_replayDock->toggleViewAction());
    }
    // --- 新增：添加视图菜单项 ---
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitViewAction);
    viewMenu->addAction(m_fitViewTimeAction);
    viewMenu->addAction(m_fitViewYAction);
    // --- -------------------- ---
}

/**
 * @brief 创建视图工具栏 (用于游标和重放)
 */
void MainWindow::createToolBars()
{
    m_viewToolBar = new QToolBar(tr("View Toolbar"), this);

    // “弹簧”小部件，将所有动作推到右侧
    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_viewToolBar->addWidget(spacer);

    // --- 新增：添加视图缩放按钮 ---
    m_viewToolBar->addAction(m_fitViewAction);
    m_viewToolBar->addAction(m_fitViewTimeAction);
    m_viewToolBar->addAction(m_fitViewYAction);
    m_viewToolBar->addSeparator();
    // --- -------------------- ---

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
 * @brief 创建底部重放停靠栏
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

    // 错误 2 修正：QDockWidget 必须添加到 DockWidgetArea
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
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    plot->legend->setVisible(true);

    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);

    // --- 修改：连接新的鼠标事件处理器 ---
    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotMousePress);
    connect(plot, &QCustomPlot::mouseMove, this, &MainWindow::onPlotMouseMove);
    connect(plot, &QCustomPlot::mouseRelease, this, &MainWindow::onPlotMouseRelease);
    // --- -------------------------- ---

    // X轴同步
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &MainWindow::onXAxisRangeChanged);
}

void MainWindow::clearPlotLayout()
{
    // --- 重点修改 ---
    // 清除游标
    clearCursors();

    // 清理布局中的所有 QFrame (及其 QCustomPlot 子控件)
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

    // 清除所有运行时查找表和指针
    // *不要* 清除 m_plotSignalMap，因为它存储持久状态
    m_plotWidgets.clear();
    m_activePlot = nullptr;
    m_plotFrameMap.clear();
    m_plotGraphMap.clear();  // 必须清除，因为 QCustomPlot* 键已失效
    m_plotWidgetMap.clear(); // 必须清除，因为 QCustomPlot* 键已失效
    m_lastMousePlot = nullptr;
}

void MainWindow::setupPlotLayout(int rows, int cols)
{
    // --- 持久化逻辑开始 ---
    // (在 clearPlotLayout 之前不需要保存，因为 m_plotSignalMap 已经是持久的)
    clearPlotLayout(); // 清理旧布局 (这会清空 m_plotWidgets, m_plotGraphMap 等)

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    if (!grid)
    {
        grid = new QGridLayout(m_plotContainer);
    }

    QCPRange sharedXRange;
    bool hasSharedXRange = false;

    // 1. 恢复图表
    if (m_signalTreeModel && !m_loadedTimeData.isEmpty())
    {
        // 遍历所有持久化存储的子图索引
        for (int plotIndex : m_plotSignalMap.keys())
        {
            // 计算此索引在新布局中的行/列
            int r = plotIndex / cols;
            int c = plotIndex % cols;

            // 如果此索引在新布局中仍然可见
            if (r < rows && c < cols)
            {
                // --- 创建 Plot ---
                QFrame *plotFrame = new QFrame(m_plotContainer);
                plotFrame->setFrameShape(QFrame::NoFrame);
                plotFrame->setLineWidth(2);
                plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

                QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
                frameLayout->setContentsMargins(0, 0, 0, 0);

                QCustomPlot *plot = new QCustomPlot(plotFrame);
                // setupPlotInteractions 将在下面统一调用

                frameLayout->addWidget(plot);
                grid->addWidget(plotFrame, r, c);
                m_plotWidgets.append(plot); // m_plotWidgets 被重新填充
                m_plotFrameMap.insert(plot, plotFrame);
                m_plotWidgetMap.insert(plot, plotIndex);
                // --- ---------------- ---

                // --- 恢复信号 ---
                const QSet<int> &signalIndices = m_plotSignalMap.value(plotIndex);
                for (int signalIndex : signalIndices)
                {
                    QStandardItem *item = m_signalTreeModel->item(signalIndex);
                    if (item)
                    {
                        QCPGraph *graph = plot->addGraph();
                        graph->setName(item->text());
                        graph->setData(m_loadedTimeData, m_loadedValueData[signalIndex]);
                        graph->setPen(item->data(PenDataRole).value<QPen>());
                        // 重新填充 m_plotGraphMap
                        m_plotGraphMap[plot].insert(signalIndex, graph);
                    }
                }
                plot->rescaleAxes();
                if (!hasSharedXRange)
                {
                    sharedXRange = plot->xAxis->range();
                    hasSharedXRange = true;
                }
                else
                {
                    plot->xAxis->setRange(sharedXRange);
                }
                plot->replot();
            }
        }
    }

    // 2. 创建剩余的空 plot
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            int plotIndex = r * cols + c;
            if (m_plotWidgetMap.key(plotIndex)) // 这个 plot 已经创建过了
                continue;

            // --- 创建 Plot ---
            QFrame *plotFrame = new QFrame(m_plotContainer);
            plotFrame->setFrameShape(QFrame::NoFrame);
            plotFrame->setLineWidth(2);
            plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

            QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
            frameLayout->setContentsMargins(0, 0, 0, 0);

            QCustomPlot *plot = new QCustomPlot(plotFrame);
            // setupPlotInteractions 将在下面统一调用

            frameLayout->addWidget(plot);
            grid->addWidget(plotFrame, r, c);
            m_plotWidgets.append(plot); // m_plotWidgets 被重新填充
            m_plotFrameMap.insert(plot, plotFrame);
            m_plotWidgetMap.insert(plot, plotIndex);

            if (hasSharedXRange)
            {
                plot->xAxis->setRange(sharedXRange);
            }
            // --- ---------------- ---
        }
    }

    // 3. 为所有新创建的 plot 设置交互
    for (QCustomPlot *plot : m_plotWidgets)
    {
        setupPlotInteractions(plot);
    }
    // --- -------------------------- ---

    if (!m_plotWidgets.isEmpty())
    {
        // 尝试重新激活之前活动的子图，如果它仍然存在的话
        int activePlotIndex = m_activePlot ? m_plotWidgetMap.value(m_activePlot, 0) : 0;

        QCustomPlot *newActivePlot = m_plotWidgetMap.key(activePlotIndex, m_plotWidgets.first());
        m_activePlot = newActivePlot;

        m_lastMousePlot = m_activePlot;
        QFrame *frame = m_plotFrameMap.value(m_activePlot);
        if (frame)
        {
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        }
        updateSignalTreeChecks(); // <-- 这将更新新 m_activePlot 的复选框
        m_signalTree->viewport()->update();
    }

    // 布局更改后，重建游标
    setupCursors();
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

void MainWindow::on_actionLoadFile_triggered()
{
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
    m_progressDialog->setValue(percentage);
}

void MainWindow::onDataLoadFinished(const CsvData &data)
{
    m_progressDialog->hide();
    qDebug() << "Main Thread: Load finished. Received" << data.timeData.count() << "data points.";

    // 1. 缓存数据
    m_loadedTimeData = data.timeData;
    m_loadedValueData = data.valueData;

    // --- 重点修改：加载新数据时，清空所有状态 ---
    // 2. 清理所有图表和持久化状态
    m_plotSignalMap.clear(); // 清除持久化状态
    for (QCustomPlot *plot : m_plotWidgets)
    {
        plot->clearGraphs();
        // plot->replot(); // 将在 setupPlotLayout 中重绘
    }
    m_plotGraphMap.clear(); // 清除运行时 map

    // 强制重新设置布局（例如 1x1），这将清除所有旧控件
    // 并根据（现在为空的）m_plotSignalMap 创建新控件
    setupPlotLayout(1, 1);
    // --- ---------------------------------- ---

    // 填充信号树
    {
        QSignalBlocker blocker(m_signalTreeModel);
        populateSignalTree(data);
    }
    m_signalTree->reset();

    // 4. 更新勾选状态
    updateSignalTreeChecks();

    QMessageBox::information(this, tr("Success"), tr("Successfully loaded %1 data points.").arg(data.timeData.count()));

    // 更新重放控件和游标
    if (!m_loadedTimeData.isEmpty())
    {
        // 将游标 1 移动到数据起点
        m_cursorKey1 = m_loadedTimeData.first();
        m_cursorKey2 = m_loadedTimeData.first() + getGlobalTimeRange().size() * 0.1; // 游标 2 在 10% 处
        updateCursors(m_cursorKey1, 1);
        updateCursors(m_cursorKey2, 2);
    }
    updateReplayControls(); // 设置滑块范围

    // --- 新增：数据加载完成后自动缩放视图 ---
    on_actionFitView_triggered();
    // ---------------------------------
}

void MainWindow::populateSignalTree(const CsvData &data)
{
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
        item->setData(i - 1, Qt::UserRole); // <-- 信号索引 (i-1) 存储在 UserRole 中

        QColor color(10 + QRandomGenerator::global()->bounded(245),
                     10 + QRandomGenerator::global()->bounded(245),
                     10 + QRandomGenerator::global()->bounded(245));
        QPen pen(color, 1);
        m_signalPens.append(pen);
        item->setData(QVariant::fromValue(pen), PenDataRole);

        m_signalTreeModel->appendRow(item); // <-- 行号 (row) 恰好等于信号索引 (i-1)
    }
}

void MainWindow::onDataLoadFailed(const QString &errorString)
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), errorString);
}

void MainWindow::onPlotClicked()
{
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot || clickedPlot == m_activePlot)
        return;

    // --- 修正：使用 m_plotWidgetMap 查找索引 ---
    int plotIndex = m_plotWidgetMap.value(clickedPlot, -1);
    if (plotIndex == -1)
        return;

    // 取消高亮旧的 active plot
    if (m_activePlot)
    {
        QFrame *oldFrame = m_plotFrameMap.value(m_activePlot);
        if (oldFrame)
            oldFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");
    }

    m_activePlot = clickedPlot;
    m_lastMousePlot = clickedPlot;

    // 高亮新的 active plot
    QFrame *frame = m_plotFrameMap.value(m_activePlot);
    if (frame)
    {
        frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
    }

    qDebug() << "Active plot set to:" << m_activePlot << "(Index: " << plotIndex << ")";
    updateSignalTreeChecks();
    m_signalTree->viewport()->update();
}

void MainWindow::updateSignalTreeChecks()
{
    QSignalBlocker blocker(m_signalTreeModel);

    // --- 修正：使用 m_plotSignalMap 和 m_plotWidgetMap ---
    int activePlotIndex = m_plotWidgetMap.value(m_activePlot, -1);
    const auto &activeSignals = m_plotSignalMap.value(activePlotIndex); // 获取 QSet<int>

    for (int i = 0; i < m_signalTreeModel->rowCount(); ++i)
    {
        QStandardItem *item = m_signalTreeModel->item(i);
        if (!item)
            continue;
        int signalIndex = item->data(Qt::UserRole).toInt();

        // 使用 QSet::contains()
        if (activeSignals.contains(signalIndex))
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
    {
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
        }
        return;
    }

    // --- 修正：使用 m_plotWidgetMap ---
    int plotIndex = m_plotWidgetMap.value(m_activePlot, -1);
    if (!m_activePlot || plotIndex == -1) // 检查 m_activePlot 是否为 null 并且索引有效
    // --- ------------------------- ---
    {
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
    {
        qWarning() << "Invalid signal index" << signalIndex;
        return;
    }

    QSignalBlocker blocker(m_signalTreeModel);
    if (item->checkState() == Qt::Checked)
    {
        // --- 修正：使用 m_plotSignalMap ---
        if (m_plotSignalMap.value(plotIndex).contains(signalIndex))
        // --- ------------------------- ---
        {
            qWarning() << "Graph already exists on this plot.";
            return;
        }
        qDebug() << "Adding signal" << signalName << "(index" << signalIndex << ") to plot" << m_activePlot;
        QCPGraph *graph = m_activePlot->addGraph();
        graph->setName(signalName);
        graph->setData(m_loadedTimeData, m_loadedValueData[signalIndex]);
        QPen pen = item->data(PenDataRole).value<QPen>();
        graph->setPen(pen);

        // --- 修正：同时更新两个 Map ---
        m_plotGraphMap[m_activePlot].insert(signalIndex, graph);
        m_plotSignalMap[plotIndex].insert(signalIndex);
        // --- ------------------------- ---

        m_activePlot->rescaleAxes();
        m_activePlot->replot();
    }
    else // Qt::Unchecked
    {
        // --- 修正：使用 m_plotSignalMap 和辅助函数 ---
        if (!m_plotSignalMap.value(plotIndex).contains(signalIndex))
        // --- ------------------------- ---
        {
            return;
        }
        // --- 修正：使用辅助函数 ---
        QCPGraph *graph = getGraph(m_activePlot, signalIndex);
        // --- ------------------------- ---
        if (graph)
        {
            qDebug() << "Removing signal" << signalName << "from plot" << m_activePlot;
            m_activePlot->removeGraph(graph); // removeGraph 会 delete graph

            // --- 修正：同时更新两个 Map ---
            m_plotGraphMap[m_activePlot].remove(signalIndex);
            m_plotSignalMap[plotIndex].remove(signalIndex);
            // --- ------------------------- ---

            m_activePlot->replot();
        }
    }

    // 添加/删除 graph 后，重建游标以包含/排除它
    setupCursors();
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
}

void MainWindow::onSignalItemDoubleClicked(const QModelIndex &index)
{
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

    // --- 修正：遍历 m_plotGraphMap ---
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
    // --- ------------------------- ---
}

/**
 * @brief 响应游标模式切换
 */
void MainWindow::onCursorModeChanged(QAction *action)
{
    // --- 修正：在启用/禁用游标时，始终保持平移开启 ---
    for (QCustomPlot *plot : m_plotWidgets)
    {
        plot->setInteraction(QCP::iRangeDrag, true);
    }

    if (action == m_cursorNoneAction)
    {
        m_cursorMode = NoCursor;
    }
    else
    {
        if (action == m_cursorSingleAction)
            m_cursorMode = SingleCursor;
        else if (action == m_cursorDoubleAction)
            m_cursorMode = DoubleCursor;
    }
    // --- ----------------------------------------- ---

    // 重建游标 UI
    setupCursors();
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
}

/**
 * @brief 响应重放按钮切换
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

// ---
// ---
// --- 游标拖拽逻辑开始
// ---
// ---

/**
 * @brief 响应 Plot 上的鼠标按下
 */
void MainWindow::onPlotMousePress(QMouseEvent *event)
{
    if (m_cursorMode == NoCursor)
        return; // 游标未激活

    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    // 检查是否点击了游标 1
    if (m_cursorMode == SingleCursor || m_cursorMode == DoubleCursor)
    {
        int plotIndex = m_plotWidgets.indexOf(plot);
        if (plotIndex != -1 && plotIndex < m_cursorLines1.size())
        {
            // 检查与线的距离
            double dist1 = m_cursorLines1.at(plotIndex)->selectTest(event->pos(), false);
            if (dist1 >= 0 && dist1 < plot->selectionTolerance())
            {
                m_isDraggingCursor1 = true;
                // --- 修正：暂时禁用平移 ---
                plot->setInteraction(QCP::iRangeDrag, false);
                event->accept(); // 接受事件，阻止 QCustomPlot 的 iRangeDrag
                return;          // 优先拖动游标 1
            }
        }
    }

    // 检查是否点击了游标 2
    if (m_cursorMode == DoubleCursor)
    {
        int plotIndex = m_plotWidgets.indexOf(plot);
        if (plotIndex != -1 && plotIndex < m_cursorLines2.size())
        {
            double dist2 = m_cursorLines2.at(plotIndex)->selectTest(event->pos(), false);
            if (dist2 >= 0 && dist2 < plot->selectionTolerance())
            {
                m_isDraggingCursor2 = true;
                // --- 修正：暂时禁用平移 ---
                plot->setInteraction(QCP::iRangeDrag, false);
                event->accept(); // 接受事件，阻止 QCustomPlot 的 iRangeDrag
                return;
            }
        }
    }

    // 如果没有点击游标，则不处理事件 (event->ignore() 是默认的)
    // 这将允许 QCustomPlot 的 iRangeDrag (平移) 生效
}

/**
 * @brief 响应 Plot 上的鼠标移动
 */
void MainWindow::onPlotMouseMove(QMouseEvent *event)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (plot)
        m_lastMousePlot = plot; // 记录最后交互的 plot
    else if (m_lastMousePlot)
        plot = m_lastMousePlot; // 后备
    else if (!m_plotWidgets.isEmpty())
        plot = m_plotWidgets.first(); // 最终后备
    else
        return; // 没有 plot

    // 从 plot 获取 x 坐标
    double key = plot->xAxis->pixelToCoord(event->pos().x());

    // --- 拖拽逻辑 ---
    if (m_isDraggingCursor1)
    {
        updateCursors(key, 1);
        event->accept();
    }
    else if (m_isDraggingCursor2)
    {
        updateCursors(key, 2);
        event->accept();
    }
    // --- 悬停逻辑 ---
    else if (m_cursorMode != NoCursor)
    {
        int plotIndex = m_plotWidgets.indexOf(plot);
        if (plotIndex == -1)
        {
            plot->setCursor(Qt::ArrowCursor);
            return;
        }

        bool nearCursor = false;
        if (plotIndex < m_cursorLines1.size())
        {
            double dist1 = m_cursorLines1.at(plotIndex)->selectTest(event->pos(), false);
            if (dist1 >= 0 && dist1 < plot->selectionTolerance())
                nearCursor = true;
        }

        if (!nearCursor && m_cursorMode == DoubleCursor && plotIndex < m_cursorLines2.size())
        {
            double dist2 = m_cursorLines2.at(plotIndex)->selectTest(event->pos(), false);
            if (dist2 >= 0 && dist2 < plot->selectionTolerance())
                nearCursor = true;
        }

        if (nearCursor)
            plot->setCursor(Qt::SizeHorCursor);
        else
            plot->setCursor(Qt::ArrowCursor);
    }
    else
    {
        plot->setCursor(Qt::ArrowCursor);
    }
}

/**
 * @brief 响应 Plot 上的鼠标释放
 */
void MainWindow::onPlotMouseRelease(QMouseEvent *event)
{
    Q_UNUSED(event);

    // --- 修正：重新启用平移 ---
    if (m_isDraggingCursor1 || m_isDraggingCursor2)
    {
        // 必须找到正确的 plot 指针，sender() 可能不可靠
        QCustomPlot *plot = m_lastMousePlot;
        if (plot && m_cursorMode != NoCursor)
        {
            // 拖动游标后，重新启用平移
            plot->setInteraction(QCP::iRangeDrag, true);
        }
    }
    // --- ----------------------- ---

    m_isDraggingCursor1 = false;
    m_isDraggingCursor2 = false;
}

// ---
// ---
// --- 游标拖拽逻辑结束
// ---
// ---

/**
 * @brief 清除所有游标图元
 */
void MainWindow::clearCursors()
{
    qDeleteAll(m_cursorLines1);
    m_cursorLines1.clear();
    qDeleteAll(m_cursorLines2);
    m_cursorLines2.clear();

    // --- 修改：清理新的标签 ---
    qDeleteAll(m_cursorXLabels1);
    m_cursorXLabels1.clear();
    qDeleteAll(m_cursorXLabels2);
    m_cursorXLabels2.clear();

    qDeleteAll(m_cursorYLabels1);
    m_cursorYLabels1.clear();
    qDeleteAll(m_cursorYLabels2);
    m_cursorYLabels2.clear();
    // --- -------------------- ---

    qDeleteAll(m_graphTracers1);
    m_graphTracers1.clear();
    qDeleteAll(m_graphTracers2);
    m_graphTracers2.clear();
}

/**
 * @brief 根据 m_cursorMode 设置游标
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

    // ---
    // --- 重点修改：创建新的游标和标签
    // ---
    QColor xLabelBgColor(255, 255, 255, 200);
    QBrush xLabelBrush(xLabelBgColor);
    QPen xLabelPen(Qt::black);

    // 1. 为每个 Plot 创建垂直线 和 X轴标签
    for (QCustomPlot *plot : m_plotWidgets)
    {
        // --- 创建游标 1 ---
        QCPItemLine *line1 = new QCPItemLine(plot);
        line1->setPen(pen1);
        line1->setSelectable(true); // 使其可被 selectTest 命中
        // --- 修正：使用绝对像素坐标 ---
        line1->start->setType(QCPItemPosition::ptAbsolute);
        line1->end->setType(QCPItemPosition::ptAbsolute);
        line1->setClipToAxisRect(true); // 裁剪到轴矩形
        m_cursorLines1.append(line1);

        QCPItemText *xLabel1 = new QCPItemText(plot);
        xLabel1->setLayer("overlay");
        xLabel1->setClipToAxisRect(false);
        xLabel1->setPadding(QMargins(5, 2, 5, 2));
        xLabel1->setBrush(xLabelBrush);
        xLabel1->setPen(xLabelPen);
        xLabel1->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter); // 顶部中心锚定
        xLabel1->position->setParentAnchor(line1->start);               // 锚定到线的底部
        xLabel1->position->setCoords(0, 5);                             // 偏移量 (0, 5)
        m_cursorXLabels1.append(xLabel1);

        if (m_cursorMode == DoubleCursor)
        {
            // --- 创建游标 2 ---
            QCPItemLine *line2 = new QCPItemLine(plot);
            line2->setPen(pen2);
            line2->setSelectable(true);
            // --- 修正：使用绝对像素坐标 ---
            line2->start->setType(QCPItemPosition::ptAbsolute);
            line2->end->setType(QCPItemPosition::ptAbsolute);
            line2->setClipToAxisRect(true);
            m_cursorLines2.append(line2);

            QCPItemText *xLabel2 = new QCPItemText(plot);
            xLabel2->setLayer("overlay");
            xLabel2->setClipToAxisRect(false);
            xLabel2->setPadding(QMargins(5, 2, 5, 2));
            xLabel2->setBrush(xLabelBrush);
            xLabel2->setPen(xLabelPen);
            xLabel2->setPositionAlignment(Qt::AlignTop | Qt::AlignHCenter);
            xLabel2->position->setParentAnchor(line2->start);
            xLabel2->position->setCoords(0, 5);
            m_cursorXLabels2.append(xLabel2);
        }
    }

    // 2. 为每个 Graph 创建跟踪器 和 Y轴标签
    for (auto it = m_plotGraphMap.begin(); it != m_plotGraphMap.end(); ++it)
    {
        QCustomPlot *plot = it.key(); // 获取 QCustomPlot
        for (QCPGraph *graph : it.value())
        {
            // --- 游标 1 Y标签 ---
            QCPItemTracer *tracer1 = new QCPItemTracer(plot);
            tracer1->setGraph(graph);
            tracer1->setInterpolating(true);
            tracer1->setVisible(false); // 跟踪器本身不可见
            m_graphTracers1.insert(graph, tracer1);

            QCPItemText *yLabel1 = new QCPItemText(plot);
            yLabel1->setLayer("overlay");
            yLabel1->setClipToAxisRect(false);
            yLabel1->setPadding(QMargins(5, 2, 5, 2));
            yLabel1->setBrush(QBrush(QColor(255, 255, 255, 180)));
            yLabel1->setPen(QPen(graph->pen().color())); // 标签边框颜色
            yLabel1->setColor(graph->pen().color());     // 文本颜色
            yLabel1->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            yLabel1->position->setParentAnchor(tracer1->position); // 锚定到跟踪器
            yLabel1->position->setCoords(5, 0);                    // 偏移量 (5, 0)
            m_cursorYLabels1.insert(tracer1, yLabel1);

            if (m_cursorMode == DoubleCursor)
            {
                // --- 游标 2 Y标签 ---
                QCPItemTracer *tracer2 = new QCPItemTracer(plot);
                tracer2->setGraph(graph);
                tracer2->setInterpolating(true);
                tracer2->setVisible(false);
                m_graphTracers2.insert(graph, tracer2);

                QCPItemText *yLabel2 = new QCPItemText(plot);
                yLabel2->setLayer("overlay");
                yLabel2->setClipToAxisRect(false);
                yLabel2->setPadding(QMargins(5, 2, 5, 2));
                yLabel2->setBrush(QBrush(QColor(255, 255, 255, 180)));
                yLabel2->setPen(QPen(graph->pen().color()));
                yLabel2->setColor(graph->pen().color());
                yLabel2->setPositionAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                yLabel2->position->setParentAnchor(tracer2->position);
                yLabel2->position->setCoords(5, 0);
                m_cursorYLabels2.insert(tracer2, yLabel2);
            }
        }
    }

    // --- -------------------------- ---

    // 确保所有子图都重绘
    for (QCustomPlot *plot : m_plotWidgets)
    {
        plot->replot();
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
    if (range.size() <= 0) // 如果没有数据，不要更新
        return;

    if (key < range.lower)
        key = range.lower;
    if (key > range.upper)
        key = range.upper;

    // 2. 存储 key
    QList<QCPItemLine *> *lines;
    QMap<QCPGraph *, QCPItemTracer *> *tracers;
    QList<QCPItemText *> *xLabels;
    QMap<QCPItemTracer *, QCPItemText *> *yLabels;

    if (cursorIndex == 1)
    {
        m_cursorKey1 = key;
        lines = &m_cursorLines1;
        tracers = &m_graphTracers1;
        xLabels = &m_cursorXLabels1;
        yLabels = &m_cursorYLabels1;
    }
    else if (cursorIndex == 2 && m_cursorMode == DoubleCursor)
    {
        m_cursorKey2 = key;
        lines = &m_cursorLines2;
        tracers = &m_graphTracers2;
        xLabels = &m_cursorXLabels2;
        yLabels = &m_cursorYLabels2;
    }
    else
    {
        return; // 无效
    }

    // ---
    // --- 重点修改：更新新的标签
    // ---

    // 3. 遍历所有 plot，更新它们的游标
    for (int i = 0; i < m_plotWidgets.size(); ++i)
    {
        QCustomPlot *plot = m_plotWidgets.at(i);
        if (i >= lines->size())
            continue; // 安全检查

        // A. 更新垂直线 (使用绝对像素坐标)
        double xPixel = plot->xAxis->coordToPixel(key);
        QCPItemLine *line = lines->at(i);
        // --- 修正：确保线横跨整个轴矩形 ---
        line->start->setCoords(xPixel, plot->axisRect()->bottom());
        line->end->setCoords(xPixel, plot->axisRect()->top());
        // --- ------------------------- ---

        // B. 更新 X 轴文本标签
        QCPItemText *xLabel = xLabels->at(i);
        xLabel->setText(QString::number(key, 'f', 4));
        // (位置会自动更新，因为它锚定在 line->start 上)

        // C. 遍历此 plot 上的所有 graph，更新 Y 轴标签
        if (m_plotGraphMap.contains(plot))
        {
            for (QCPGraph *graph : m_plotGraphMap.value(plot))
            {
                if (tracers->contains(graph))
                {
                    QCPItemTracer *tracer = tracers->value(graph);
                    tracer->setGraphKey(key);

                    // --- 修正：手动调用 updatePosition ---
                    tracer->updatePosition();
                    // --- ------------------------- ---

                    QCPItemText *yLabel = yLabels->value(tracer, nullptr);
                    if (yLabel)
                    {
                        double value = tracer->position->value(); // 现在获取的是新值
                        yLabel->setText(QString::number(value, 'f', 3));
                        yLabel->setVisible(true);
                    }
                }
            }
        }

        // D. 如果有双光标，计算差值 (只在活动 plot 上显示)
        // (省略，因为这会使标签变得混乱。可以添加一个单独的 QCPItemText 来显示差值)
        /*
        if (m_cursorMode == DoubleCursor && plot == m_activePlot)
        {
            ...
        }
        */

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
 * @brief  获取全局时间范围
 */
QCPRange MainWindow::getGlobalTimeRange() const
{
    if (m_loadedTimeData.isEmpty())
        return QCPRange(0, 1);

    return QCPRange(m_loadedTimeData.first(), m_loadedTimeData.last());
}

/**
 * @brief 估算数据时间步
 */
double MainWindow::findDataTimeStep() const
{
    if (m_loadedTimeData.size() < 2)
        return 0.01; // 默认步长

    // 假设时间步长基本恒定
    return m_loadedTimeData.at(1) - m_loadedTimeData.at(0);
}

/**
 * @brief 播放/暂停按钮点击
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
 * @brief  重放定时器触发
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
 * @brief 步进按钮
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
 * @brief 步退按钮
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
 * @brief 时间滑块被用户拖动
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

// --- 新增：视图缩放槽函数实现 ---

/**
 * @brief [槽] 适应视图大小 (所有子图, X 和 Y 轴)
 */
void MainWindow::on_actionFitView_triggered()
{
    if (m_plotWidgets.isEmpty())
        return;

    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot && plot->graphCount() > 0)
        {
            plot->rescaleAxes();
            plot->replot();
        }
    }
}

/**
 * @brief [槽] 适应视图大小 (所有子图, 仅 X 轴)
 */
void MainWindow::on_actionFitViewTime_triggered()
{
    if (m_plotWidgets.isEmpty())
        return;

    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot && plot->graphCount() > 0)
        {
            plot->xAxis->rescale();
            plot->replot();
        }
    }
}

/**
 * @brief [槽] 适应视图大小 (仅活动子图, 仅 Y 轴)
 */
void MainWindow::on_actionFitViewY_triggered()
{
    // --- 修正：仅缩放当前X轴范围内的Y轴 ---
    if (m_activePlot && m_activePlot->graphCount() > 0)
    {
        QCPRange keyRange = m_activePlot->xAxis->range();
        QCPRange valueRange;
        bool foundRange = false;

        // 遍历活动子图上的所有图表
        // --- 修正：使用 m_plotGraphMap ---
        const auto &graphs = m_plotGraphMap.value(m_activePlot);
        for (QCPGraph *graph : graphs)
        // --- ------------------------- ---
        {
            bool currentFound = false;
            // 获取该图表在当前X轴范围内的Y值范围
            QCPRange graphValueRange = graph->getValueRange(currentFound, QCP::sdBoth, keyRange);
            if (currentFound)
            {
                if (!foundRange)
                    valueRange = graphValueRange;
                else
                    valueRange.expand(graphValueRange);
                foundRange = true;
            }
        }

        // 设置Y轴范围并重绘
        if (foundRange)
        {
            m_activePlot->yAxis->setRange(valueRange);
            m_activePlot->replot();
        }
    }
}
// --- ------------------------- ---

// --- 新增：X轴同步槽函数实现 ---
/**
 * @brief [槽] 当一个X轴范围改变时，同步所有其他的X轴
 */
void MainWindow::onXAxisRangeChanged(const QCPRange &newRange)
{
    QObject *senderAxis = sender();
    if (!senderAxis)
        return;

    for (QCustomPlot *plot : m_plotWidgets)
    {
        // 仅更新 *其他* 图表, 避免信号循环
        if (plot->xAxis != senderAxis)
        {
            // 阻止此 setRange 再次发出 rangeChanged 信号
            QSignalBlocker blocker(plot->xAxis);
            plot->xAxis->setRange(newRange);

            // 如果Y轴也需要同步（例如，如果它们是链接的），
            // 在这里添加逻辑。目前，我们只重绘以更新游标。
            plot->replot();
        }
    }

    // X轴变化时，游标也需要更新
    // （因为Y轴范围可能已改变，标签需要重新定位）
    if (m_cursorMode != NoCursor)
    {
        updateCursors(m_cursorKey1, 1);
        if (m_cursorMode == DoubleCursor)
            updateCursors(m_cursorKey2, 2);
    }
}
// --- ----------------------- ---

// --- 新增：辅助函数 ---
/**
 * @brief 从 m_plotGraphMap 中安全地获取一个 QCPGraph*
 * @param plot QCustomPlot 控件
 * @param signalIndex 信号索引
 * @return 如果找到则返回 QCPGraph*，否则返回 nullptr
 */
QCPGraph *MainWindow::getGraph(QCustomPlot *plot, int signalIndex) const
{
    if (plot && m_plotGraphMap.contains(plot))
    {
        return m_plotGraphMap.value(plot).value(signalIndex, nullptr);
    }
    return nullptr;
}
// --- ---------------- ---