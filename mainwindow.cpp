#include "mainwindow.h"
#include "qcustomplot.h"
#include "signaltreedelegate.h"     // <-- 包含自定义委托
#include "signalpropertiesdialog.h" // <-- 新增：包含新对话框的头文件

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
#include <QStandardItem>
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
#include <QFileInfo> // 用于获取文件名
#include <QCursor>   // 用于获取鼠标位置

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_dataThread(nullptr), m_dataManager(nullptr), m_plotContainer(nullptr), m_signalDock(nullptr), m_signalTree(nullptr), m_signalTreeModel(nullptr), m_progressDialog(nullptr), m_activePlot(nullptr), m_lastMousePlot(nullptr), m_cursorMode(NoCursor), m_cursorKey1(0), m_cursorKey2(0),
      m_isDraggingCursor1(false),
      m_isDraggingCursor2(false)
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
    connect(this, &MainWindow::requestLoadMat, m_dataManager, &DataManager::loadMatFile, Qt::QueuedConnection);
    // --- --------------------- ---
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
    m_loadFileAction = new QAction(tr("&Load File..."), this);
    m_loadFileAction->setShortcut(QKeySequence::Open);
    connect(m_loadFileAction, &QAction::triggered, this, &MainWindow::on_actionLoadFile_triggered);

    // 布局菜单
    m_layout1x1Action = new QAction(tr("1x1 Layout"), this);
    connect(m_layout1x1Action, &QAction::triggered, this, &MainWindow::on_actionLayout1x1_triggered);
    m_layout2x2Action = new QAction(tr("2x2 Layout"), this);
    connect(m_layout2x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout2x2_triggered);
    m_layout3x2Action = new QAction(tr("3x2 Layout"), this);
    connect(m_layout3x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout3x2_triggered);

    // 视图缩放动作
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
    // 添加视图菜单项
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitViewAction);
    viewMenu->addAction(m_fitViewTimeAction);
    viewMenu->addAction(m_fitViewYAction);
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

    // 添加视图缩放按钮 ---
    m_viewToolBar->addAction(m_fitViewAction);
    m_viewToolBar->addAction(m_fitViewTimeAction);
    m_viewToolBar->addAction(m_fitViewYAction);
    m_viewToolBar->addSeparator();

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

    // --- 新增：连接右键菜单 ---
    m_signalTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onSignalTreeContextMenu);
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

    addDockWidget(Qt::BottomDockWidgetArea, m_replayDock); // 使用 BottomDockWidgetArea 替代错误的 BottomToolBarArea

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

    // X轴同步
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &MainWindow::onXAxisRangeChanged);
}

void MainWindow::clearPlotLayout()
{

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
    m_plotWidgets.clear();
    m_activePlot = nullptr;
    m_plotFrameMap.clear();
    m_plotGraphMap.clear();
    m_plotWidgetMap.clear();
    m_lastMousePlot = nullptr;
}

void MainWindow::setupPlotLayout(int rows, int cols)
{
    clearPlotLayout(); // 清理旧布局 (这会清空 m_plotWidgets, m_plotGraphMap 等)

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    if (!grid)
    {
        grid = new QGridLayout(m_plotContainer);
    }

    QCPRange sharedXRange;
    bool hasSharedXRange = false;

    // 从 m_plotSignalMap 恢复
    // 1. 恢复图表
    if (m_signalTreeModel && !m_fileDataMap.isEmpty()) // <-- 修改
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
                // 创建 Plot
                QFrame *plotFrame = new QFrame(m_plotContainer);
                plotFrame->setFrameShape(QFrame::NoFrame);
                plotFrame->setLineWidth(2);
                plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

                QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
                frameLayout->setContentsMargins(0, 0, 0, 0);

                QCustomPlot *plot = new QCustomPlot(plotFrame);

                frameLayout->addWidget(plot);
                grid->addWidget(plotFrame, r, c);
                m_plotWidgets.append(plot); // m_plotWidgets 被重新填充
                m_plotFrameMap.insert(plot, plotFrame);
                m_plotWidgetMap.insert(plot, plotIndex);

                // 恢复信号
                const QSet<QString> &signalIDs = m_plotSignalMap.value(plotIndex);
                for (const QString &uniqueID : signalIDs)
                {

                    QStringList parts = uniqueID.split('/');
                    if (parts.size() != 3)
                        continue;
                    QString filename = parts[0];
                    QString tablename = parts[1];
                    int signalIndex = parts[2].toInt();

                    // 查找 QStandardItem
                    QList<QStandardItem *> fileItems = m_signalTreeModel->findItems(filename, Qt::MatchExactly);
                    if (fileItems.isEmpty())
                        continue;

                    // 树中查找表和信号
                    QStandardItem *tableItem = nullptr;
                    for (int t_idx = 0; t_idx < fileItems.first()->rowCount(); ++t_idx)
                    {
                        if (fileItems.first()->child(t_idx)->text() == tablename) // 假设表名是唯一的
                        {
                            tableItem = fileItems.first()->child(t_idx);
                            break;
                        }
                    }
                    if (!tableItem || tableItem->rowCount() <= signalIndex)
                        continue;
                    QStandardItem *item = tableItem->child(signalIndex);

                    // 查找 FileData
                    if (item && m_fileDataMap.contains(filename))
                    {
                        const FileData &fileData = m_fileDataMap.value(filename);

                        // 查找 SignalTable
                        const SignalTable *tableData = nullptr;
                        for (const auto &table : fileData.tables)
                        {
                            if (table.name == tablename)
                            {
                                tableData = &table;
                                break;
                            }
                        }
                        if (!tableData || signalIndex >= tableData->valueData.size())
                            continue;

                        QCPGraph *graph = plot->addGraph();
                        graph->setName(item->text());
                        // <-- 重点：使用特定文件和表的数据 -->
                        graph->setData(tableData->timeData, tableData->valueData[signalIndex]);
                        graph->setPen(item->data(PenDataRole).value<QPen>());
                        // 重新填充 m_plotGraphMap
                        m_plotGraphMap[plot].insert(uniqueID, graph);
                    }
                }

                plot->rescaleAxes();
                if (!hasSharedXRange && plot->graphCount() > 0) // 确保有图表再设置
                {
                    sharedXRange = plot->xAxis->range();
                    hasSharedXRange = true;
                }
                else if (hasSharedXRange)
                {
                    plot->xAxis->setRange(sharedXRange);
                }
                plot->replot();
            }
        }
    }
    // --- ---------------------------------- ---

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

            frameLayout->addWidget(plot);
            grid->addWidget(plotFrame, r, c);
            m_plotWidgets.append(plot); // m_plotWidgets 被重新填充
            m_plotFrameMap.insert(plot, plotFrame);
            m_plotWidgetMap.insert(plot, plotIndex);

            if (hasSharedXRange)
            {
                plot->xAxis->setRange(sharedXRange);
            }
        }
    }

    // 3. 为所有新创建的 plot 设置交互
    for (QCustomPlot *plot : m_plotWidgets)
    {
        setupPlotInteractions(plot);
    }

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

    if (m_cursorMode != NoCursor)
    {
        // 使用 singleShot 连接到新槽
        QTimer::singleShot(0, this, &MainWindow::updateCursorsForLayoutChange);
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

void MainWindow::on_actionLoadFile_triggered()
{
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Open File"), "", tr("Data Files (*.csv *.txt *.mat)"));
    if (filePath.isEmpty())
        return;

    m_progressDialog->setValue(0);
    m_progressDialog->setLabelText(tr("Loading %1...").arg(QFileInfo(filePath).fileName()));
    m_progressDialog->show();

    // --- 新增：检查文件类型 ---
    if (filePath.endsWith(".mat", Qt::CaseInsensitive))
    {
        emit requestLoadMat(filePath);
    }
    else
    {
        emit requestLoadCsv(filePath);
    }
}

void MainWindow::showLoadProgress(int percentage)
{
    m_progressDialog->setValue(percentage);
}

void MainWindow::onDataLoadFinished(const FileData &data)
{
    m_progressDialog->hide();
    qDebug() << "Main Thread: Load finished for" << data.filePath;

    QString filename = QFileInfo(data.filePath).fileName();
    // 检查文件是否已加载
    if (m_fileDataMap.contains(filename))
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("File exists"),
                                      tr("File '%1' is already loaded. Do you want to overwrite it?").arg(filename),
                                      QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::No)
        {
            return;
        }
        // 如果用户选择 "Yes"，则移除旧文件
        removeFile(filename);
    }

    // 1. 缓存数据
    m_fileDataMap.insert(filename, data);

    // 2. 填充信号树
    {
        QSignalBlocker blocker(m_signalTreeModel);
        populateSignalTree(data); // <-- 传入新数据
    }
    m_signalTree->reset(); // <-- CSV 和 MAT 都需要

    // 3. 更新重放控件和游标
    if (m_fileDataMap.size() == 1 && !data.tables.isEmpty() && !data.tables.first().timeData.isEmpty()) // 如果这是加载的第一个文件
    {
        const SignalTable &firstTable = data.tables.first();
        // 将游标 1 移动到数据起点
        m_cursorKey1 = firstTable.timeData.first();
        m_cursorKey2 = firstTable.timeData.first() + getGlobalTimeRange().size() * 0.1; // 游标 2 在 10% 处
        updateCursors(m_cursorKey1, 1);
        updateCursors(m_cursorKey2, 2);

        // 数据加载完成后自动缩放视图
        on_actionFitView_triggered();
    }
    else
    {
        // 如果已有数据，仅更新范围并重绘
        on_actionFitView_triggered(); // 重新缩放以包含新数据
        updateCursors(m_cursorKey1, 1);
        updateCursors(m_cursorKey2, 2);
    }
    updateReplayControls(); // 设置滑块范围
}

void MainWindow::populateSignalTree(const FileData &data)
{
    QString filename = QFileInfo(data.filePath).fileName();

    QStandardItem *fileItem = new QStandardItem(filename);
    fileItem->setEditable(false);
    fileItem->setCheckable(false); // 文件条目本身不可勾选
    fileItem->setData(filename, FileNameRole);
    fileItem->setData(true, IsFileItemRole);
    fileItem->setData(false, IsSignalItemRole);
    m_signalTreeModel->appendRow(fileItem);

    // 遍历所有表 (对于 CSV，只有一个表)
    for (int t_idx = 0; t_idx < data.tables.size(); ++t_idx)
    {
        const SignalTable &table = data.tables.at(t_idx);

        // 如果只有一个表，并且其名称与文件名相同，则跳过创建表节点
        bool skipTableNode = (data.tables.size() == 1 && table.name == QFileInfo(filename).completeBaseName());

        QStandardItem *parentItem = fileItem;
        if (!skipTableNode)
        {
            QStandardItem *tableItem = new QStandardItem(table.name);
            tableItem->setEditable(false);
            tableItem->setCheckable(false);
            tableItem->setData(filename, FileNameRole);
            tableItem->setData(false, IsFileItemRole);
            tableItem->setData(false, IsSignalItemRole); // <-- 新增
            fileItem->appendRow(tableItem);
            parentItem = tableItem; // 信号将附加到表条目
        }

        // 使用表中的 headers
        for (int i = 0; i < table.headers.count(); ++i)
        {
            QString signalName = table.headers[i].trimmed();
            if (signalName.isEmpty())
                signalName = tr("Signal %1").arg(i + 1);

            QStandardItem *item = new QStandardItem(signalName);
            item->setEditable(false);
            item->setCheckable(true);
            item->setCheckState(Qt::Unchecked);

            // 新的 UniqueID 格式 "filename/tablename/signalindex"
            QString uniqueID = QString("%1/%2/%3").arg(filename).arg(table.name).arg(i);

            item->setData(uniqueID, UniqueIdRole);
            item->setData(false, IsFileItemRole);
            item->setData(true, IsSignalItemRole);
            item->setData(filename, FileNameRole);

            QColor color(10 + QRandomGenerator::global()->bounded(245),
                         10 + QRandomGenerator::global()->bounded(245),
                         10 + QRandomGenerator::global()->bounded(245));

            // 将默认宽度为 2
            // QPen pen(color, 2); //宽度2绘制密集线段会卡
            QPen pen(color, 1);
            // --- ---------------------------- ---

            item->setData(QVariant::fromValue(pen), PenDataRole);

            parentItem->appendRow(item); // <-- 添加到父条目 (文件或表)
        }
    }
    // --- ------------------------------- ---
}

void MainWindow::onDataLoadFailed(const QString &filePath, const QString &errorString) // <-- 修改
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), tr("Failed to load %1:\n%2").arg(filePath).arg(errorString));
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
    const auto &activeSignals = m_plotSignalMap.value(activePlotIndex); // 获取 QSet<QString>

    // --- 修改：遍历树形结构 (文件 -> 表 -> 信号) ---
    for (int i = 0; i < m_signalTreeModel->rowCount(); ++i)
    {
        QStandardItem *fileItem = m_signalTreeModel->item(i);
        if (!fileItem)
            continue;
        for (int j = 0; j < fileItem->rowCount(); ++j)
        {
            QStandardItem *tableItem = fileItem->child(j);
            if (!tableItem)
                continue;

            // 检查是表节点还是直接的信号节点
            if (tableItem->data(IsSignalItemRole).toBool())
            {
                // 这是 (CSV) 信号项
                QString uniqueID = tableItem->data(UniqueIdRole).toString();
                if (activeSignals.contains(uniqueID))
                    tableItem->setCheckState(Qt::Checked);
                else
                    tableItem->setCheckState(Qt::Unchecked);
            }
            else
            {
                // 这是 (MAT) 表项，遍历其子信号项
                for (int k = 0; k < tableItem->rowCount(); ++k)
                {
                    QStandardItem *signalItem = tableItem->child(k);
                    if (signalItem)
                    {
                        QString uniqueID = signalItem->data(UniqueIdRole).toString();
                        if (activeSignals.contains(uniqueID))
                            signalItem->setCheckState(Qt::Checked);
                        else
                            signalItem->setCheckState(Qt::Unchecked);
                    }
                }
            }
        }
    }
}

void MainWindow::onSignalItemChanged(QStandardItem *item)
{
    if (!item)
        return;

    // --- 修改：如果是文件或表条目，则忽略 ---
    if (!item->data(IsSignalItemRole).toBool())
    {
        return;
    }
    // --- ---------------------------- ---

    if (m_fileDataMap.isEmpty()) // <-- 修改
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

    // --- 修改：使用 UniqueID ---
    QString uniqueID = item->data(UniqueIdRole).toString();
    QString signalName = item->text();
    if (uniqueID.isEmpty())
    {
        qWarning() << "Invalid signal item" << signalName;
        return;
    }
    // --- ----------------------- ---

    QSignalBlocker blocker(m_signalTreeModel);
    if (item->checkState() == Qt::Checked)
    {
        // --- 修正：使用 m_plotSignalMap ---
        if (m_plotSignalMap.value(plotIndex).contains(uniqueID))
        // --- ------------------------- ---
        {
            qWarning() << "Graph already exists on this plot.";
            return;
        }
        qDebug() << "Adding signal" << signalName << "(id" << uniqueID << ") to plot" << m_activePlot;

        // --- 修改：从 uniqueID 获取数据 ---
        QStringList parts = uniqueID.split('/');
        if (parts.size() < 2) // 至少 "filename/signalindex" 或 "filename/tablename/signalindex"
            return;

        QString filename = parts[0];
        if (!m_fileDataMap.contains(filename))
            return;
        const FileData &fileData = m_fileDataMap.value(filename);

        const SignalTable *tableData = nullptr;
        int signalIndex = -1;

        if (parts.size() == 2) // CSV 格式: "filename/signalindex" (表名被跳过)
        {
            if (fileData.tables.isEmpty())
                return;
            tableData = &fileData.tables.first();
            signalIndex = parts[1].toInt();
        }
        else if (parts.size() == 3) // MAT 格式: "filename/tablename/signalindex"
        {
            QString tablename = parts[1];
            signalIndex = parts[2].toInt();
            for (const auto &table : fileData.tables)
            {
                if (table.name == tablename)
                {
                    tableData = &table;
                    break;
                }
            }
        }

        if (!tableData || signalIndex < 0 || signalIndex >= tableData->valueData.size())
            return;
        // --- ------------------------- ---

        QCPGraph *graph = m_activePlot->addGraph();
        graph->setName(signalName);
        graph->setData(tableData->timeData, tableData->valueData[signalIndex]); // <-- 使用特定文件和表的数据
        QPen pen = item->data(PenDataRole).value<QPen>();
        graph->setPen(pen);

        // --- 修正：同时更新两个 Map ---
        m_plotGraphMap[m_activePlot].insert(uniqueID, graph);
        m_plotSignalMap[plotIndex].insert(uniqueID);
        // --- ------------------------- ---

        m_activePlot->rescaleAxes();
        m_activePlot->replot();
    }
    else // Qt::Unchecked
    {
        // --- 修正：使用 m_plotSignalMap 和辅助函数 ---
        if (!m_plotSignalMap.value(plotIndex).contains(uniqueID))
        // --- ------------------------- ---
        {
            return;
        }
        // --- 修正：使用辅助函数 ---
        QCPGraph *graph = getGraph(m_activePlot, uniqueID);
        // --- ------------------------- ---
        if (graph)
        {
            qDebug() << "Removing signal" << signalName << "from plot" << m_activePlot;
            m_activePlot->removeGraph(graph); // removeGraph 会 delete graph

            // --- 修正：同时更新两个 Map ---
            m_plotGraphMap[m_activePlot].remove(uniqueID);
            m_plotSignalMap[plotIndex].remove(uniqueID);
            // --- ------------------------- ---

            m_activePlot->replot();
        }
    }

    // 添加/删除 graph 后，重建游标以包含/排除它
    setupCursors();
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
}

void MainWindow::onSignalItemDoubleClicked(const QModelIndex &index) // <-- 替换此函数
{
    if (!index.isValid())
        return;
    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);
    // --- 1. 检查是否为信号条目 ---
    if (!item || !item->data(IsSignalItemRole).toBool())
        return;

    // --- 2. 检查点击位置 ---
    QPoint localPos = m_signalTree->viewport()->mapFromGlobal(QCursor::pos());
    QRect itemRect = m_signalTree->visualRect(index);

    // 这些值必须与 SignalTreeDelegate::paint 中的值匹配
    const int previewWidth = 40;
    const int margin = 2;

    // 计算预览线本身的可点击区域
    QRect previewClickRect(
        itemRect.right() - previewWidth + margin, // 预览区域的左边缘 + 边距
        itemRect.top(),
        previewWidth - (2 * margin), // 只在两个边距之间
        itemRect.height());

    // 如果点击不在预览线区域，则忽略
    if (!previewClickRect.contains(localPos))
    {
        return; // 用户点击了文本，不是预览线
    }

    // --- 3. (修改) 如果点击在预览线上，则打开新对话框 ---
    QString uniqueID = item->data(UniqueIdRole).toString();
    QPen currentPen = item->data(PenDataRole).value<QPen>();

    // --- 使用新的自定义对话框 ---
    SignalPropertiesDialog dialog(currentPen, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return; // 用户点击了 "Cancel"
    }

    QPen newPen = dialog.getSelectedPen(); // 获取包含所有属性的新 QPen
    // --- ------------------------- ---

    item->setData(QVariant::fromValue(newPen), PenDataRole);

    // 更新所有图表中该信号的画笔
    for (auto it = m_plotGraphMap.begin(); it != m_plotGraphMap.end(); ++it)
    {
        if (it.value().contains(uniqueID))
        {
            QCPGraph *graph = it.value().value(uniqueID);
            if (graph)
            {
                graph->setPen(newPen);
                graph->parentPlot()->replot();
            }
        }
    }
}

// --- 新增：信号树的右键菜单槽 ---
void MainWindow::onSignalTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = m_signalTree->indexAt(pos);
    if (!index.isValid())
        return;

    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);
    // --- 修改：只在文件条目上显示菜单 ---
    if (!item || !item->data(IsFileItemRole).toBool())
        return; // 只在文件条目上显示菜单
    // --- ---------------------------- ---

    QString filename = item->data(FileNameRole).toString(); // <-- 修改：使用 FileNameRole

    QMenu contextMenu(this);
    QAction *deleteAction = contextMenu.addAction(tr("Remove '%1'").arg(filename));
    deleteAction->setData(filename); // 将文件名存储在动作中

    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteFileAction);
    contextMenu.exec(m_signalTree->viewport()->mapToGlobal(pos));
}

// --- 新增：删除文件的动作 ---
void MainWindow::onDeleteFileAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    QString filename = action->data().toString();
    if (filename.isEmpty())
        return;

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Remove File"),
                                  tr("Are you sure you want to remove all data and graphs from file '%1'?").arg(filename),
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
    {
        removeFile(filename);
    }
}

// --- 新增：移除文件的辅助函数 ---
void MainWindow::removeFile(const QString &filename)
{
    // 1. 从数据 map 中移除
    if (!m_fileDataMap.remove(filename))
    {
        qWarning() << "File not found in data map:" << filename;
        return;
    }

    // 2. 从图表和持久化 map 中移除
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (!m_plotGraphMap.contains(plot))
            continue;

        int plotIndex = m_plotWidgetMap.value(plot, -1);
        if (plotIndex == -1)
            continue;

        QMap<QString, QCPGraph *> &graphMap = m_plotGraphMap[plot];
        QSet<QString> &signalSet = m_plotSignalMap[plotIndex];

        // 查找所有属于此文件的 unique IDs
        // --- 修改：新的 ID 格式 ---
        QString prefix = filename + "/";
        // --- ------------------ ---
        QList<QString> idsToRemove;
        for (const QString &uniqueID : graphMap.keys())
        {
            if (uniqueID.startsWith(prefix))
            {
                idsToRemove.append(uniqueID);
            }
        }

        // 移除图表
        for (const QString &uniqueID : idsToRemove)
        {
            QCPGraph *graph = graphMap.value(uniqueID);
            if (graph)
            {
                plot->removeGraph(graph); // removeGraph 会 delete graph
            }
            graphMap.remove(uniqueID);
            signalSet.remove(uniqueID);
        }
        plot->replot();
    }

    // 3. 从信号树中移除
    QList<QStandardItem *> items = m_signalTreeModel->findItems(filename);
    for (QStandardItem *item : items)
    {
        // --- 修改：确保我们得到的是顶层文件条目 ---
        if (item->data(IsFileItemRole).toBool() && item->parent() == nullptr)
        // --- --------------------------------- ---
        {
            m_signalTreeModel->removeRow(item->row());
            break; // 假设文件名是唯一的
        }
    }

    // 4. 清理和更新
    setupCursors(); // 重建游标 (删除的图表会自动从 m_plotGraphMap 中移除)
    updateCursors(m_cursorKey1, 1);
    updateCursors(m_cursorKey2, 2);
    updateReplayControls();
    on_actionFitView_triggered(); // 重新缩放视图
}

// --- ----------------------------- ---

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
 * @brief 销毁所有游标项
 */
void MainWindow::clearCursors()
{
    // 辅助lambda函数，用于安全地移除 item
    // QCustomPlot::removeItem 会处理 delete 和从内部 mItems 列表的移除
    auto safeRemoveItem = [](QCPAbstractItem *item)
    {
        if (item && item->parentPlot())
        {
            // 这是正确的方法：让 QCustomPlot 移除并删除它拥有的 item
            item->parentPlot()->removeItem(item);
        }
        else if (item)
        {
            // 作为后备，如果 item 没有父 plot（理论上不应发生），
            // 我们仍然需要 delete 它以避免内存泄漏。
            qWarning() << "clearCursors: Item has no parent plot, deleting directly.";
            delete item;
        }
    };

    // --- 重点修改：用 safeRemoveItem 替换 qDeleteAll ---

    // 遍历所有 QMap 的 values() 并移除 item
    // 注意：Y 标签（QCPItemText）依赖于 Tracers (QCPItemTracer) 作为父锚点
    // 我们应该先移除子项（Y标签），再移除父项（Tracers）。
    // QCustomPlot::removeItem 会处理 item 的析构，
    // 而 QCPItemPosition 的析构函数会通知其子项它正在被删除，
    // 所以移除顺序可能不重要，但先移除子项更安全。

    for (QCPItemText *item : m_cursorYLabels1.values())
        safeRemoveItem(item);
    m_cursorYLabels1.clear();
    for (QCPItemText *item : m_cursorYLabels2.values())
        safeRemoveItem(item);
    m_cursorYLabels2.clear();

    // 移除 Tracers
    for (QCPItemTracer *item : m_graphTracers1.values())
        safeRemoveItem(item);
    m_graphTracers1.clear();
    for (QCPItemTracer *item : m_graphTracers2.values())
        safeRemoveItem(item);
    m_graphTracers2.clear();

    // 遍历所有 QList 并移除 item
    for (QCPItemLine *item : m_cursorLines1)
        safeRemoveItem(item);
    m_cursorLines1.clear();
    for (QCPItemLine *item : m_cursorLines2)
        safeRemoveItem(item);
    m_cursorLines2.clear();
    for (QCPItemText *item : m_cursorXLabels1)
        safeRemoveItem(item);
    m_cursorXLabels1.clear();
    for (QCPItemText *item : m_cursorXLabels2)
        safeRemoveItem(item);
    m_cursorXLabels2.clear();

    // 此时，所有 item 都已通过 QCustomPlot::removeItem 安全删除。
    // 我们的指针列表也已清空。
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
            tracer1->setInterpolating(false); // <-- 不插值，吸附到最近的点

            // tracer1->setVisible(false); // 跟踪器本身不可见
            tracer1->setVisible(true);
            tracer1->setStyle(QCPItemTracer::tsCircle);      // 设置为圆形
            tracer1->setSize(3);                             // "加粗" (10像素)
            tracer1->setPen(graph->pen());                   // 轮廓颜色与图表线相同
            tracer1->setBrush(QBrush(graph->pen().color())); // 填充颜色与图表线相同
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
                tracer2->setInterpolating(false); // <-- 不插值，吸附到最近的点
                // tracer2->setVisible(false);
                tracer2->setVisible(true);
                tracer2->setStyle(QCPItemTracer::tsCircle);      // 设置为圆形
                tracer2->setSize(3);                             // "加粗" (10像素)
                tracer2->setPen(graph->pen());                   // 轮廓颜色与图表线相同
                tracer2->setBrush(QBrush(graph->pen().color())); // 填充颜色与图表线相同
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
    if (m_fileDataMap.isEmpty()) // <-- 修改
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
    // --- 修改：遍历所有文件 ---
    if (m_fileDataMap.isEmpty())
        return QCPRange(0, 1);

    bool first = true;
    QCPRange totalRange;
    for (const FileData &data : m_fileDataMap.values())
    {
        // --- 新增：遍历所有表 ---
        for (const SignalTable &table : data.tables)
        {
            if (!table.timeData.isEmpty())
            {
                if (first)
                {
                    totalRange.lower = table.timeData.first();
                    totalRange.upper = table.timeData.last();
                    first = false;
                }
                else
                {
                    if (table.timeData.first() < totalRange.lower)
                        totalRange.lower = table.timeData.first();
                    if (table.timeData.last() > totalRange.upper)
                        totalRange.upper = table.timeData.last();
                }
            }
        }
        // --- ----------------- ---
    }

    if (first) // 意味着没有文件有数据
        return QCPRange(0, 1);
    else
        return totalRange;
    // --- ------------------- ---
}

/**
 * @brief 估算数据时间步
 */
double MainWindow::getSmallestTimeStep() const
{
    // --- 修改：查找所有文件中的最小步长 ---
    double minStep = -1.0;

    for (const FileData &data : m_fileDataMap.values())
    {
        // --- 新增：遍历所有表 ---
        for (const SignalTable &table : data.tables)
        {
            if (table.timeData.size() >= 2)
            {
                double step = table.timeData.at(1) - table.timeData.at(0);
                if (step > 0 && (minStep == -1.0 || step < minStep))
                {
                    minStep = step;
                }
            }
        }
        // --- ----------------- ---
    }

    return (minStep > 0) ? minStep : 0.01; // 默认步长
    // --- ---------------------------- ---
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
        // --- 修改：使用固定的 33ms 间隔 (约 30fps) ---
        m_replayTimer->setInterval(33);
        m_replayTimer->start();
        m_playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        // --- ------------------------------------ ---
    }
}

/**
 * @brief  重放定时器触发
 */
void MainWindow::onReplayTimerTimeout()
{
    double speed = m_speedSpinBox->value();
    // --- 修改：时间步长取决于定时器间隔和速度 ---
    double timeStep = (m_replayTimer->interval() / 1000.0) * speed;
    // --- ------------------------------------ ---

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

    // 速度可能已更改，但我们保持间隔不变
}

/**
 * @brief 步进按钮
 */
void MainWindow::onStepForwardClicked()
{
    if (m_replayTimer->isActive())
        return;

    double timeStep = getSmallestTimeStep(); // <-- 修改
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

    double timeStep = getSmallestTimeStep(); // <-- 修改
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

    // --- 修改：单独缩放每个图表的 Y 轴，但同步 X 轴 ---
    QCPRange globalXRange;
    bool hasXRange = false;

    // 第一次遍历：找到全局 X 范围并缩放 Y 轴
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot && plot->graphCount() > 0)
        {
            plot->rescaleAxes(false); // 缩放 Y 轴
            if (!hasXRange)
            {
                globalXRange = plot->xAxis->range();
                hasXRange = true;
            }
            else
            {
                globalXRange.expand(plot->xAxis->range());
            }
            // plot->replot(); // 稍后在第二次遍历中重绘
        }
    }

    // 第二次遍历：应用全局 X 范围并重绘
    if (hasXRange)
    {
        for (QCustomPlot *plot : m_plotWidgets)
        {
            if (plot)
            {
                plot->xAxis->setRange(globalXRange);
                plot->replot();
            }
        }
    }
    // --- -------------------------------------- ---
}

/**
 * @brief [槽] 适应视图大小 (所有子图, 仅 X 轴)
 */
void MainWindow::on_actionFitViewTime_triggered()
{
    if (m_plotWidgets.isEmpty())
        return;

    // --- 修改：找到全局 X 范围并应用 ---
    QCPRange globalXRange;
    bool hasXRange = false;

    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot && plot->graphCount() > 0)
        {
            plot->xAxis->rescale();
            if (!hasXRange)
            {
                globalXRange = plot->xAxis->range();
                hasXRange = true;
            }
            else
            {
                globalXRange.expand(plot->xAxis->range());
            }
        }
    }

    if (hasXRange)
    {
        for (QCustomPlot *plot : m_plotWidgets)
        {
            if (plot)
            {
                plot->xAxis->setRange(globalXRange);
                plot->replot();
            }
        }
    }
    // --- ---------------------------- ---
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
 * @brief [修改] 从 m_plotGraphMap 中安全地获取一个 QCPGraph*
 * @param plot QCustomPlot 控件
 * @param uniqueID 信号的唯一ID ("filename/tablename/signalindex")
 * @return 如果找到则返回 QCPGraph*，否则返回 nullptr
 */
QCPGraph *MainWindow::getGraph(QCustomPlot *plot, const QString &uniqueID) const
{
    if (plot && m_plotGraphMap.contains(plot))
    {
        return m_plotGraphMap.value(plot).value(uniqueID, nullptr);
    }
    return nullptr;
}

/**
 * @brief [修改] 从 QStandardItem 构建 UniqueID
 */
QString MainWindow::getUniqueID(QStandardItem *item) const
{
    if (!item || !item->data(IsSignalItemRole).toBool()) // <-- 修改
        return QString();

    // --- 修改：UniqueID 现在直接存储在条目中 ---
    return item->data(UniqueIdRole).toString();
    // --- ------------------------------------ ---
}
// --- ---------------- ---

/**
 * @brief [槽] 在布局更改和重绘完成后更新游标位置
 */
void MainWindow::updateCursorsForLayoutChange()
{
    if (m_cursorMode != NoCursor)
    {
        updateCursors(m_cursorKey1, 1);
        if (m_cursorMode == DoubleCursor)
        {
            updateCursors(m_cursorKey2, 2);
        }
    }
}