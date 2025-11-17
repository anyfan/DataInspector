#include "mainwindow.h"
#include "qcustomplot.h"
#include "signaltreedelegate.h"
#include "signalpropertiesdialog.h"
#include "replaymanager.h"

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
#include <QStyle>
#include <QIcon>
#include <QFileInfo>
#include <QCursor>
#include <QFormLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QEvent>

/**
 * @brief [辅助函数] 通过 UniqueIdRole 在模型中迭代查找 QStandardItem (广度优先)
 * @param model 要搜索的 QStandardItemModel
 * @param uniqueID 要查找的 ID
 * @return 找到的 QStandardItem，如果未找到则返回 nullptr
 */
static QStandardItem *findItemByUniqueID_BFS(QStandardItemModel *model, const QString &uniqueID)
{
    if (!model)
        return nullptr;

    QList<QStandardItem *> itemsToSearch;
    // 从根项开始
    QStandardItem *root = model->invisibleRootItem();
    for (int i = 0; i < root->rowCount(); ++i)
    {
        itemsToSearch.append(root->child(i));
    }

    int head = 0;
    while (head < itemsToSearch.size())
    {
        QStandardItem *currentItem = itemsToSearch.at(head++); // 获取并移除队列头部
        if (!currentItem)
            continue;

        // 检查此项
        if (currentItem->data(UniqueIdRole).toString() == uniqueID)
        {
            return currentItem;
        }

        // 将子项添加到队列尾部
        if (currentItem->hasChildren())
        {
            for (int i = 0; i < currentItem->rowCount(); ++i)
            {
                itemsToSearch.append(currentItem->child(i));
            }
        }
    }

    return nullptr; // 未找到
}
// --- ---------------- ---

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_dataThread(nullptr),
      m_dataManager(nullptr),
      m_plotContainer(nullptr),
      m_signalDock(nullptr),
      m_signalTree(nullptr),
      m_signalTreeModel(nullptr),
      m_progressDialog(nullptr),
      m_activePlot(nullptr),
      m_lastMousePlot(nullptr),
      m_loadFileAction(nullptr),
      m_layout1x1Action(nullptr),
      m_layout1x2Action(nullptr),
      m_layout2x1Action(nullptr),
      m_layout2x2Action(nullptr),
      m_layoutSplitBottomAction(nullptr),
      m_layoutSplitLeftAction(nullptr),
      m_layoutSplitTopAction(nullptr),
      m_layoutSplitRightAction(nullptr),
      m_layoutCustomAction(nullptr),
      m_viewToolBar(nullptr),
      m_cursorNoneAction(nullptr),
      m_cursorSingleAction(nullptr),
      m_cursorDoubleAction(nullptr),
      m_replayAction(nullptr),
      m_cursorGroup(nullptr),
      m_fitViewAction(nullptr),
      m_fitViewTimeAction(nullptr),
      m_fitViewYAction(nullptr),
      m_toggleLegendAction(nullptr),
      m_customLayoutDialog(nullptr),
      m_customRowsSpinBox(nullptr),
      m_customColsSpinBox(nullptr),
      m_cursorManager(nullptr),
      m_replayManager(nullptr),
      m_openGLAction(nullptr),
      m_yAxisGroup(nullptr),
      m_colorIndex(0)
{
    setupDataManagerThread();

    m_plotContainer = new QWidget(this);
    m_plotContainer->setLayout(new QGridLayout());
    setCentralWidget(m_plotContainer);

    m_cursorManager = new CursorManager(&m_plotGraphMap, &m_plotWidgets, &m_lastMousePlot, this);

    createActions();

    m_replayManager = new ReplayManager(m_replayAction, m_cursorManager, this);

    createDocks();
    createToolBars();
    createMenus();

    // 4. 设置窗口标题和大小
    setWindowTitle(tr("Data Inspector (Async)"));
    resize(1280, 800);

    // 在构造函数中启用拖放
    setAcceptDrops(true);

    // 初始化颜色列表
    m_colorList << QColor("#0072bd"); // 蓝
    m_colorList << QColor("#d95319"); // 橙
    m_colorList << QColor("#edb120"); // 黄
    m_colorList << QColor("#7e2f8e"); // 紫
    m_colorList << QColor("#77ac30"); // 绿
    m_colorList << QColor("#4dbeee"); // 青
    m_colorList << QColor("#a2142f"); // 红

    // 扩展颜色 (7)
    m_colorList << QColor("#139fff"); // 亮蓝
    m_colorList << QColor("#ff6929"); // 亮橙
    m_colorList << QColor("#b746ff"); // 亮紫
    m_colorList << QColor("#64d413"); // 亮绿
    m_colorList << QColor("#ff13a6"); // 亮粉
    m_colorList << QColor("#fe330a"); // 亮红
    m_colorList << QColor("#22b573"); // 蓝绿

    // 5. 设置初始布局
    setupPlotLayout(2, 1);

    // 6. 创建进度对话框
    m_progressDialog = new QProgressDialog(this);
    m_progressDialog->reset();
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setAutoClose(true);
    m_progressDialog->setAutoReset(true);
    m_progressDialog->setMinimum(0);
    m_progressDialog->setMaximum(100);
    m_progressDialog->setCancelButton(nullptr);
    m_progressDialog->hide();

    // 7. 注册 QPen 类型
    qRegisterMetaType<QPen>("QPen");

    connect(m_cursorManager, &CursorManager::cursorKeyChanged,
            m_replayManager, &ReplayManager::onCursorKeyChanged);
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

    // 替换布局菜单
    m_layout1x1Action = new QAction(tr("1x1 Layout"), this);
    connect(m_layout1x1Action, &QAction::triggered, this, &MainWindow::on_actionLayout1x1_triggered);

    m_layout1x2Action = new QAction(tr("1x2 Layout (Side by Side)"), this);
    connect(m_layout1x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout1x2_triggered);

    m_layout2x1Action = new QAction(tr("2x1 Layout (Stacked)"), this);
    connect(m_layout2x1Action, &QAction::triggered, this, &MainWindow::on_actionLayout2x1_triggered);

    m_layout2x2Action = new QAction(tr("2x2 Layout"), this);
    connect(m_layout2x2Action, &QAction::triggered, this, &MainWindow::on_actionLayout2x2_triggered);

    m_layoutSplitBottomAction = new QAction(tr("Bottom Split"), this);
    connect(m_layoutSplitBottomAction, &QAction::triggered, this, &MainWindow::on_actionLayoutSplitBottom_triggered);

    m_layoutSplitTopAction = new QAction(tr("Top Split"), this);
    connect(m_layoutSplitTopAction, &QAction::triggered, this, &MainWindow::on_actionLayoutSplitTop_triggered);

    m_layoutSplitLeftAction = new QAction(tr("Left Split"), this);
    connect(m_layoutSplitLeftAction, &QAction::triggered, this, &MainWindow::on_actionLayoutSplitLeft_triggered);

    m_layoutSplitRightAction = new QAction(tr("Right Split"), this);
    connect(m_layoutSplitRightAction, &QAction::triggered, this, &MainWindow::on_actionLayoutSplitRight_triggered);

    m_layoutCustomAction = new QAction(tr("Custom Grid..."), this);
    connect(m_layoutCustomAction, &QAction::triggered, this, &MainWindow::on_actionLayoutCustom_triggered);

    // 视图缩放动作
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    m_fitViewAction->setToolTip(tr("适应视图"));
    connect(m_fitViewAction, &QAction::triggered, this, &MainWindow::on_actionFitView_triggered);

    m_fitViewTimeAction = new QAction(tr("Fit View (Time)"), this);
    m_fitViewTimeAction->setIcon(QIcon::fromTheme("zoom-fit-width", style()->standardIcon(QStyle::SP_ArrowRight)));
    m_fitViewTimeAction->setToolTip(tr("适应视图（时间轴）"));
    connect(m_fitViewTimeAction, &QAction::triggered, this, &MainWindow::on_actionFitViewTime_triggered);

    m_fitViewYAction = new QAction(tr("Fit View (Y-Axis)"), this);
    m_fitViewYAction->setIcon(QIcon::fromTheme("zoom-fit-height", style()->standardIcon(QStyle::SP_ArrowDown)));
    m_fitViewYAction->setToolTip(tr("适应视图（Y轴）"));
    connect(m_fitViewYAction, &QAction::triggered, this, &MainWindow::on_actionFitViewY_triggered);

    // 视图/游标动作
    m_cursorNoneAction = new QAction(tr("关闭游标"), this);
    m_cursorNoneAction->setCheckable(true);
    m_cursorNoneAction->setChecked(true);

    m_cursorSingleAction = new QAction(tr("单游标"), this);
    m_cursorSingleAction->setCheckable(true);

    m_cursorDoubleAction = new QAction(tr("双游标"), this);
    m_cursorDoubleAction->setCheckable(true);

    m_cursorGroup = new QActionGroup(this);
    m_cursorGroup->addAction(m_cursorNoneAction);
    m_cursorGroup->addAction(m_cursorSingleAction);
    m_cursorGroup->addAction(m_cursorDoubleAction);
    connect(m_cursorGroup, &QActionGroup::triggered, m_cursorManager, &CursorManager::onCursorActionTriggered);

    m_replayAction = new QAction(tr("重放"), this);
    m_replayAction->setCheckable(true);
    connect(m_replayAction, &QAction::toggled, this, &MainWindow::onReplayActionToggled);

    // 图例切换动作
    m_toggleLegendAction = new QAction(tr("切换图例"), this);
    m_toggleLegendAction->setCheckable(true);
    m_toggleLegendAction->setChecked(true);                                                 // 默认图例是可见的
    m_toggleLegendAction->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation)); // 暂时使用一个占位图标
    m_toggleLegendAction->setToolTip(tr("显示/隐藏图例"));
    connect(m_toggleLegendAction, &QAction::toggled, this, &MainWindow::on_actionToggleLegend_toggled);

    // 创建 OpenGL 动作 ---
    m_openGLAction = new QAction(tr("启用 OpenGL 加速"), this);
    m_openGLAction->setToolTip(tr("切换 QCustomPlot 的 OpenGL 渲染。"));
    m_openGLAction->setCheckable(true);
    m_openGLAction->setChecked(false); // 默认关闭
    connect(m_openGLAction, &QAction::toggled, this, &MainWindow::onOpenGLActionToggled);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&文件"));
    fileMenu->addAction(m_loadFileAction);

    QMenu *layoutMenu = menuBar()->addMenu(tr("&布局"));
    layoutMenu->addAction(m_layout1x1Action);
    layoutMenu->addSeparator();
    layoutMenu->addAction(m_layout1x2Action);
    layoutMenu->addAction(m_layout2x1Action);
    layoutMenu->addAction(m_layout2x2Action);
    layoutMenu->addSeparator();
    layoutMenu->addAction(m_layoutSplitTopAction);
    layoutMenu->addAction(m_layoutSplitBottomAction);
    layoutMenu->addAction(m_layoutSplitLeftAction);
    layoutMenu->addAction(m_layoutSplitRightAction);
    layoutMenu->addSeparator();
    layoutMenu->addAction(m_layoutCustomAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&显示"));
    if (m_signalDock)
    {
        viewMenu->addAction(m_signalDock->toggleViewAction());
    }
    // 重放面板菜单项
    if (m_replayManager && m_replayManager->getDockWidget())
    {
        viewMenu->addAction(m_replayManager->getDockWidget()->toggleViewAction());
    }
    // 添加视图菜单项
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitViewAction);
    viewMenu->addAction(m_fitViewTimeAction);
    viewMenu->addAction(m_fitViewYAction);

    // 添加图例切换菜单项
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleLegendAction);

    // --- 创建 "设置" 菜单 ---
    QMenu *settingsMenu = menuBar()->addMenu(tr("&设置"));
    settingsMenu->addAction(m_openGLAction);
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

    // 添加视图缩放按钮
    m_viewToolBar->addAction(m_fitViewAction);
    m_viewToolBar->addAction(m_fitViewTimeAction);
    m_viewToolBar->addAction(m_fitViewYAction);
    m_viewToolBar->addSeparator();

    // 添加图例切换按钮
    m_viewToolBar->addAction(m_toggleLegendAction);
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
    m_signalDock = new QDockWidget(tr("信号"), this);

    // 创建一个容器 QWidget 来存放搜索框和树
    QWidget *dockWidget = new QWidget(m_signalDock);
    QVBoxLayout *dockLayout = new QVBoxLayout(dockWidget);
    dockLayout->setContentsMargins(4, 4, 4, 4); // 紧凑边距
    dockLayout->setSpacing(4);                  // 控件间距

    // 创建并添加搜索框
    m_signalSearchBox = new QLineEdit(dockWidget);
    m_signalSearchBox->setPlaceholderText(tr("搜索信号..."));
    m_signalSearchBox->setClearButtonEnabled(true);
    dockLayout->addWidget(m_signalSearchBox);

    m_signalTree = new QTreeView(dockWidget);
    m_signalTreeModel = new QStandardItemModel(m_signalDock);
    m_signalTree->setModel(m_signalTreeModel);
    m_signalTree->setHeaderHidden(true);
    m_signalTree->setItemDelegate(new SignalTreeDelegate(m_signalTree));

    // 启用从树状视图拖动
    m_signalTree->setDragEnabled(true);
    m_signalTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_signalTree->setSelectionMode(QAbstractItemView::ExtendedSelection); // 允许选择多行进行拖拽

    dockLayout->addWidget(m_signalTree); // <-- 将树添加到布局中

    m_signalDock->setWidget(dockWidget); // <-- 设置容器 QWidget 为 dock 的控件

    m_signalDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);

    connect(m_signalTreeModel, &QStandardItemModel::itemChanged, this, &MainWindow::onSignalItemChanged);
    connect(m_signalTree, &QTreeView::doubleClicked, this, &MainWindow::onSignalItemDoubleClicked);

    // 连接右键菜单
    m_signalTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onSignalTreeContextMenu);

    // 连接搜索框信号
    connect(m_signalSearchBox, &QLineEdit::textChanged, this, &MainWindow::onSignalSearchChanged);

    if (m_replayManager && m_replayManager->getDockWidget())
    {
        addDockWidget(Qt::BottomDockWidgetArea, m_replayManager->getDockWidget());
    }
}

void MainWindow::setupPlotInteractions(QCustomPlot *plot)
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    // --- 根据 m_toggleLegendAction 的状态设置图例可见性 ---
    plot->legend->setVisible(m_toggleLegendAction->isChecked());

    // --- 根据 m_openGLAction 的状态设置 OpenGL ---
    plot->setOpenGl(m_openGLAction->isChecked());

    QFont axisFont = plot->font();           // 从绘图控件获取基础字体
    axisFont.setPointSize(7);                // 将字号设置为 9 (你可以按需调整)
    plot->xAxis->setTickLabelFont(axisFont); // X轴的刻度数字
    plot->xAxis->setLabelFont(axisFont);     // X轴的标签
    plot->yAxis->setTickLabelFont(axisFont); // Y轴的刻度数字
    plot->yAxis->setLabelFont(axisFont);     // Y轴的标签

    // 使用 QCPMarginGroup 进行自动对齐，告诉这个子图的轴矩形，它的左边距由 m_yAxisGroup 管理
    plot->axisRect()->setMarginGroup(QCP::msLeft, m_yAxisGroup);

    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);

    // --- 连接新的鼠标事件处理器 ---
    connect(plot, &QCustomPlot::mousePress, m_cursorManager, &CursorManager::onPlotMousePress);
    connect(plot, &QCustomPlot::mouseMove, m_cursorManager, &CursorManager::onPlotMouseMove);
    connect(plot, &QCustomPlot::mouseRelease, m_cursorManager, &CursorManager::onPlotMouseRelease);

    // X轴同步
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &MainWindow::onXAxisRangeChanged);

    // 连接子图的选择信号，以同步树视图 ---
    connect(plot, &QCustomPlot::selectionChangedByUser, this, &MainWindow::onPlotSelectionChanged);

    // 设置Y轴的数字格式 ---
    // (使用 'g' 格式并设置精度，以便大数字自动切换到科学计数法)
    plot->yAxis->setNumberFormat("g");  // 'g' = 通用格式
    plot->yAxis->setNumberPrecision(4); // 精度为 4 (例如 90000 -> 9e+4)

    // 允许子图接收拖放并安装事件过滤器 ---
    plot->setAcceptDrops(true);
    plot->installEventFilter(this);

    // 连接图例交互信号 ---

    // 1. 连接图例的左键点击信号，用于切换可见性
    //    这个信号在 QCustomPlot *plot* 上，而不是在 plot->legend 上
    // connect(plot, &QCustomPlot::legendClick, this, &MainWindow::onLegendClick);

    // 2. 启用并连接图表的上下文菜单（用于图例的右键点击）
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(plot, &QCustomPlot::customContextMenuRequested, this, &MainWindow::onLegendContextMenu);
}

void MainWindow::clearPlotLayout()
{

    // 清除游标
    m_cursorManager->clearCursors();

    // 删除旧的 Y 轴边距组
    if (m_yAxisGroup)
    {
        delete m_yAxisGroup;
        m_yAxisGroup = nullptr;
    }

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

/**
 * @brief 核心布局函数，使用 QRect 列表创建网格
 * * QRect(x, y, colSpan, rowSpan)
 */
void MainWindow::setupPlotLayout(const QList<QRect> &geometries)
{
    clearPlotLayout(); // 清理旧布局 (这会清空 m_plotWidgets, m_plotGraphMap 等)

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    if (!grid)
    {
        grid = new QGridLayout(m_plotContainer);
    }

    QCPRange sharedXRange;
    bool hasSharedXRange = false;

    // 1. 创建所有新布局的 plot
    for (int i = 0; i < geometries.size(); ++i)
    {
        const QRect &geo = geometries[i];
        int plotIndex = i; // 新的 plot 索引就是列表中的索引

        // --- 创建 Plot ---
        QFrame *plotFrame = new QFrame(m_plotContainer);
        plotFrame->setFrameShape(QFrame::NoFrame);
        plotFrame->setLineWidth(2);
        plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

        QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
        frameLayout->setContentsMargins(0, 0, 0, 0);

        QCustomPlot *plot = new QCustomPlot(plotFrame);

        // --- 创建 Y 轴边距组 ---
        if (i == 0)
        {
            // 如果这是第一个创建的 plot (i == 0)，用它作为父对象来创建新的边距组
            m_yAxisGroup = new QCPMarginGroup(plot);
        }

        frameLayout->addWidget(plot);
        // --- 使用网格跨度添加 ---
        grid->addWidget(plotFrame, geo.y(), geo.x(), geo.height(), geo.width());

        m_plotWidgets.append(plot);
        m_plotFrameMap.insert(plot, plotFrame);
        m_plotWidgetMap.insert(plot, plotIndex);

        // 2. 检查 m_plotSignalMap (持久化映射) 是否包含此索引的信号
        if (m_plotSignalMap.contains(plotIndex))
        {
            const QSet<QString> &signalIDs = m_plotSignalMap.value(plotIndex);
            for (const QString &uniqueID : signalIDs)
            {
                // ... (这部分数据恢复逻辑与旧函数完全相同) ...
                QStringList parts = uniqueID.split('/');
                if (parts.size() < 2)
                    continue;

                QString filename = parts[0];
                if (!m_fileDataMap.contains(filename))
                    continue;
                const FileData &fileData = m_fileDataMap.value(filename);

                const SignalTable *tableData = nullptr;
                int signalIndex = -1;

                if (parts.size() == 2) // CSV
                {
                    if (fileData.tables.isEmpty())
                        continue;
                    tableData = &fileData.tables.first();
                    signalIndex = parts[1].toInt();
                }
                else if (parts.size() == 3) // MAT
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
                    continue;

                // 查找 QStandardItem (仅用于获取名称和画笔)
                QStandardItem *item = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);

                if (item)
                {
                    QCPGraph *graph = plot->addGraph();
                    graph->setName(item->text());
                    graph->setData(tableData->timeData, tableData->valueData[signalIndex]);
                    graph->setPen(item->data(PenDataRole).value<QPen>());
                    // 重新填充 m_plotGraphMap
                    m_plotGraphMap[plot].insert(uniqueID, graph);
                }
            } // 结束 for (signalIDs)

            plot->rescaleAxes();
            if (!hasSharedXRange && plot->graphCount() > 0)
            {
                sharedXRange = plot->xAxis->range();
                hasSharedXRange = true;
            }
            else if (hasSharedXRange)
            {
                plot->xAxis->setRange(sharedXRange);
            }
            plot->replot();
        } // 结束 if (m_plotSignalMap.contains)

        // 如果没有恢复信号，但已存在共享X轴，则应用它
        else if (hasSharedXRange)
        {
            plot->xAxis->setRange(sharedXRange);
        }

    } // 结束 for (geometries)

    // 4. 为所有新创建的 plot 设置交互
    for (QCustomPlot *plot : m_plotWidgets)
    {
        setupPlotInteractions(plot);
    }

    // 5. 设置活动子图 (逻辑与旧函数相同)
    if (!m_plotWidgets.isEmpty())
    {
        int activePlotIndex = m_activePlot ? m_plotWidgetMap.value(m_activePlot, 0) : 0;

        // 确保索引在
        if (activePlotIndex >= m_plotWidgets.size())
            activePlotIndex = 0;

        QCustomPlot *newActivePlot = m_plotWidgets.at(activePlotIndex);
        m_activePlot = newActivePlot;

        m_lastMousePlot = m_activePlot;
        QFrame *frame = m_plotFrameMap.value(m_activePlot);
        if (frame)
        {
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        }
        updateSignalTreeChecks();
        m_signalTree->viewport()->update();
    }

    // 6. 布局更改后，重建游标
    m_cursorManager->setupCursors();

    if (m_cursorManager->getMode() != CursorManager::NoCursor)
    {
        QTimer::singleShot(0, this, &MainWindow::updateCursorsForLayoutChange);
    }
}

/**
 * @brief 设置中央绘图区域的布局 (如 2x2)
 * * 这是一个辅助函数，用于调用 setupPlotLayout(const QList<QRect> &geometries)
 */
void MainWindow::setupPlotLayout(int rows, int cols)
{
    QList<QRect> geometries;
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            geometries.append(QRect(c, r, 1, 1)); // (x, y, width, height)
        }
    }
    setupPlotLayout(geometries);
}

void MainWindow::on_actionLayout1x1_triggered()
{
    setupPlotLayout(1, 1);
}

void MainWindow::on_actionLayout2x2_triggered()
{
    setupPlotLayout(2, 2);
}

void MainWindow::on_actionLayout1x2_triggered()
{
    setupPlotLayout(1, 2);
}

void MainWindow::on_actionLayout2x1_triggered()
{
    setupPlotLayout(2, 1);
}

void MainWindow::on_actionLayoutSplitBottom_triggered()
{
    QList<QRect> geometries;
    // QRect(col, row, colSpan, rowSpan)
    geometries << QRect(0, 0, 2, 1); // Top plot (跨2列)
    geometries << QRect(0, 1, 1, 1); // Bottom-left plot
    geometries << QRect(1, 1, 1, 1); // Bottom-right plot
    setupPlotLayout(geometries);
}

void MainWindow::on_actionLayoutSplitTop_triggered()
{
    QList<QRect> geometries;
    // QRect(col, row, colSpan, rowSpan)
    geometries << QRect(0, 0, 1, 1); // Top-left plot
    geometries << QRect(1, 0, 1, 1); // Top-right plot
    geometries << QRect(0, 1, 2, 1); // Bottom plot (跨2列)
    setupPlotLayout(geometries);
}

void MainWindow::on_actionLayoutSplitLeft_triggered()
{
    QList<QRect> geometries;
    // QRect(col, row, colSpan, rowSpan)
    geometries << QRect(0, 0, 1, 2); // Left plot (跨2行)
    geometries << QRect(1, 0, 1, 1); // Top-right plot
    geometries << QRect(1, 1, 1, 1); // Bottom-right plot
    setupPlotLayout(geometries);
}

void MainWindow::on_actionLayoutSplitRight_triggered()
{
    QList<QRect> geometries;
    // QRect(col, row, colSpan, rowSpan)
    geometries << QRect(0, 0, 1, 1); // Top-left plot
    geometries << QRect(0, 1, 1, 1); // Bottom-left plot
    geometries << QRect(1, 0, 1, 2); // Right plot (跨2行)
    setupPlotLayout(geometries);
}

void MainWindow::on_actionLayoutCustom_triggered()
{
    // 1. 创建对话框 (一次性)
    if (!m_customLayoutDialog)
    {
        m_customLayoutDialog = new QDialog(this);
        m_customLayoutDialog->setWindowTitle(tr("Custom Grid Layout"));

        QVBoxLayout *mainLayout = new QVBoxLayout(m_customLayoutDialog);
        QFormLayout *formLayout = new QFormLayout;

        m_customRowsSpinBox = new QSpinBox(m_customLayoutDialog);
        m_customRowsSpinBox->setRange(1, 8);
        m_customRowsSpinBox->setValue(3);

        m_customColsSpinBox = new QSpinBox(m_customLayoutDialog);
        m_customColsSpinBox->setRange(1, 8);
        m_customColsSpinBox->setValue(3);

        formLayout->addRow(tr("Rows:"), m_customRowsSpinBox);
        formLayout->addRow(tr("Columns:"), m_customColsSpinBox);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_customLayoutDialog);
        connect(buttonBox, &QDialogButtonBox::accepted, m_customLayoutDialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, m_customLayoutDialog, &QDialog::reject);

        mainLayout->addLayout(formLayout);
        mainLayout->addWidget(buttonBox);
    }

    // 2. 显示对话框并获取结果
    if (m_customLayoutDialog->exec() == QDialog::Accepted)
    {
        int rows = m_customRowsSpinBox->value();
        int cols = m_customColsSpinBox->value();
        setupPlotLayout(rows, cols);
    }
}

/**
 * @brief [槽] "Load File..." 菜单动作被触发
 * * 此函数现在只负责打开文件对话框，然后调用辅助函数 loadFile()
 */
void MainWindow::on_actionLoadFile_triggered()
{
    QString filePath = QFileDialog::getOpenFileName(this,
                                                    tr("Open File"), "", tr("Data Files (*.csv *.txt *.mat)"));

    // 只需调用新的辅助函数
    loadFile(filePath);
}

/**
 * @brief 启动加载单个文件的辅助函数
 * * 无论是通过菜单打开还是拖放，都会调用此函数
 * @param filePath 要加载的文件的路径
 */
void MainWindow::loadFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    // 注意：当拖放多个文件时，这将为每个文件显示和隐藏进度对话框
    m_progressDialog->setValue(0);
    m_progressDialog->setLabelText(tr("Loading %1...").arg(QFileInfo(filePath).fileName()));
    m_progressDialog->show();

    // 检查文件类型 ---
    if (filePath.endsWith(".mat", Qt::CaseInsensitive))
    {
        emit requestLoadMat(filePath);
    }
    else
    {
        emit requestLoadCsv(filePath);
    }
}

/**
 * @brief [重写] 当文件被拖入窗口时调用
 */
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        // 检查是否至少有一个文件是我们支持的类型
        for (const QUrl &url : event->mimeData()->urls())
        {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".csv", Qt::CaseInsensitive) ||
                filePath.endsWith(".txt", Qt::CaseInsensitive) ||
                filePath.endsWith(".mat", Qt::CaseInsensitive))
            {
                event->acceptProposedAction(); // 接受拖动
                return;
            }
        }
    }

    event->ignore(); // 否则, 拒绝拖动
}

/**
 * @brief [重写] 当文件在窗口上被放下时调用
 */
void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        QList<QUrl> urlList = mimeData->urls();
        for (const QUrl &url : urlList)
        {
            QString filePath = url.toLocalFile();
            if (!filePath.isEmpty())
            {
                // 检查文件扩展名
                if (filePath.endsWith(".csv", Qt::CaseInsensitive) ||
                    filePath.endsWith(".txt", Qt::CaseInsensitive) ||
                    filePath.endsWith(".mat", Qt::CaseInsensitive))
                {
                    loadFile(filePath); // 调用我们的辅助函数
                }
            }
        }
        event->acceptProposedAction();
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

    // 默认展开所有条目 ---
    m_signalTree->expandAll();

    // 3. 更新重放控件和游标
    if (m_fileDataMap.size() == 1 && !data.tables.isEmpty() && !data.tables.first().timeData.isEmpty()) // 如果这是加载的第一个文件
    {
        const SignalTable &firstTable = data.tables.first();
        // 将游标 1 移动到数据起点
        m_cursorManager->updateCursors(firstTable.timeData.first(), 1); // 设置初始位置
        m_cursorManager->updateCursors(firstTable.timeData.first() + getGlobalTimeRange().size() * 0.1, 2);

        // 数据加载完成后自动缩放视图
        on_actionFitView_triggered();
    }
    else
    {
        // 如果已有数据，重新缩放以包含新数据
        on_actionFitView_triggered();

        m_cursorManager->updateAllCursors();
    }
    updateReplayManagerRange(); // 设置滑块范围
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
            tableItem->setData(false, IsSignalItemRole);
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

            if (m_colorList.isEmpty()) // 安全检查
            {
                m_colorList << Qt::black;
            }

            QColor color = m_colorList.at(m_colorIndex);
            m_colorIndex = (m_colorIndex + 1) % m_colorList.size();

            // 将默认宽度为 2
            // QPen pen(color, 2); //宽度2绘制密集线段会卡
            QPen pen(color, 1);

            item->setData(QVariant::fromValue(pen), PenDataRole);

            parentItem->appendRow(item); // <-- 添加到父条目 (文件或表)
        }
    }
}

void MainWindow::onDataLoadFailed(const QString &filePath, const QString &errorString)
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), tr("Failed to load %1:\n%2").arg(filePath).arg(errorString));
}

// --- 恢复 onPlotClicked() 并使用 setActivePlot() ---
void MainWindow::onPlotClicked()
{
    // 这个槽现在只由 mousePress 信号触发，所以 sender() 总是有效的
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    setActivePlot(clickedPlot); // 调用新的辅助函数
}

/**
 * @brief 设置活动子图的辅助函数
 * (这个函数包含了上一步 onPlotClicked(QCustomPlot *plot) 的逻辑)
 * @param plot 要激活的子图
 */
void MainWindow::setActivePlot(QCustomPlot *plot)
{
    if (!plot || plot == m_activePlot)
        return;

    // --- 使用 m_plotWidgetMap 查找索引 ---
    int plotIndex = m_plotWidgetMap.value(plot, -1);
    if (plotIndex == -1)
        return;

    // 取消高亮旧的 active plot
    if (m_activePlot)
    {
        QFrame *oldFrame = m_plotFrameMap.value(m_activePlot);
        if (oldFrame)
            oldFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");
    }

    m_activePlot = plot;
    m_lastMousePlot = plot;

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

/**
 * @brief 将指定ID的信号添加到指定的子图中
 * (此逻辑从 onSignalItemChanged 提取而来)
 * @param uniqueID 要添加的信号ID
 * @param plot 目标 QCustomPlot
 */
void MainWindow::addSignalToPlot(const QString &uniqueID, QCustomPlot *plot)
{
    int plotIndex = m_plotWidgetMap.value(plot, -1);
    if (plotIndex == -1)
        return;

    // 1. 检查是否已存在
    if (m_plotSignalMap.value(plotIndex).contains(uniqueID))
    {
        qWarning() << "Graph" << uniqueID << "already exists on plot" << plot;
        return;
    }

    // 2. 查找 QStandardItem (用于获取元数据)
    QStandardItem *item = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);
    if (!item)
    {
        qWarning() << "addSignalToPlot: Could not find item in tree model for ID" << uniqueID;
        return;
    }
    QString signalName = item->text();
    QPen pen = item->data(PenDataRole).value<QPen>();

    // 3. 查找信号数据 (与 onSignalItemChanged 中的逻辑相同)
    QStringList parts = uniqueID.split('/');
    if (parts.size() < 2)
        return;
    QString filename = parts[0];
    if (!m_fileDataMap.contains(filename))
        return;
    const FileData &fileData = m_fileDataMap.value(filename);

    const SignalTable *tableData = nullptr;
    int signalIndex = -1;

    if (parts.size() == 2) // CSV
    {
        if (fileData.tables.isEmpty())
            return;
        tableData = &fileData.tables.first();
        signalIndex = parts[1].toInt();
    }
    else if (parts.size() == 3) // MAT
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

    // 4. 创建图表
    QCPGraph *graph = plot->addGraph();
    graph->setName(signalName);
    graph->setData(tableData->timeData, tableData->valueData[signalIndex]);
    graph->setPen(pen);

    // 5. 应用性能修复 (与 onSignalItemChanged 中的逻辑相同)
    if (graph->selectionDecorator())
    {
        QCPSelectionDecorator *decorator = graph->selectionDecorator();
        QPen selPen = decorator->pen();
        selPen.setWidth(pen.width());
        decorator->setPen(selPen);
        decorator->setBrush(Qt::NoBrush);
        decorator->setUsedScatterProperties(QCPScatterStyle::spNone);
    }

    // 6. 更新映射
    m_plotGraphMap[plot].insert(uniqueID, graph);
    m_plotSignalMap[plotIndex].insert(uniqueID);

    // 7. 刷新
    plot->rescaleAxes();
    plot->replot();

    // 8. 更新游标 (添加新图形后必须重建游标)
    m_cursorManager->setupCursors();
    m_cursorManager->updateAllCursors();
}

/**
 * @brief 从指定的子图中移除指定ID的信号
 * (此逻辑从 onSignalItemChanged 提取而来)
 * @param uniqueID 要移除的信号ID
 * @param plot 目标 QCustomPlot
 */
void MainWindow::removeSignalFromPlot(const QString &uniqueID, QCustomPlot *plot)
{
    int plotIndex = m_plotWidgetMap.value(plot, -1);
    if (plotIndex == -1)
        return;

    // 1. 检查是否存在
    if (!m_plotSignalMap.value(plotIndex).contains(uniqueID))
    {
        qWarning() << "Graph" << uniqueID << "does not exist on plot" << plot;
        return;
    }

    // 2. 查找图表
    QCPGraph *graph = getGraph(plot, uniqueID);
    if (graph)
    {
        // 3. 移除
        plot->removeGraph(graph); // removeGraph 会 delete graph

        // 4. 更新映射
        m_plotGraphMap[plot].remove(uniqueID);
        m_plotSignalMap[plotIndex].remove(uniqueID);

        // 5. 刷新
        plot->replot();

        // 6. 更新游标 (移除图形后必须重建游标)
        m_cursorManager->setupCursors();
        m_cursorManager->updateAllCursors();
    }
}

void MainWindow::updateSignalTreeChecks()
{
    QSignalBlocker blocker(m_signalTreeModel);

    // --- 使用 m_plotSignalMap 和 m_plotWidgetMap ---
    int activePlotIndex = m_plotWidgetMap.value(m_activePlot, -1);
    const auto &activeSignals = m_plotSignalMap.value(activePlotIndex); // 获取 QSet<QString>

    // --- 遍历树形结构 (文件 -> 表 -> 信号) ---
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

    // --- 如果是文件或表条目，则忽略 ---
    if (!item->data(IsSignalItemRole).toBool())
    {
        return;
    }

    if (m_fileDataMap.isEmpty())
    {
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
        }
        return;
    }

    // --- 使用 m_plotWidgetMap ---
    int plotIndex = m_plotWidgetMap.value(m_activePlot, -1);
    if (!m_activePlot || plotIndex == -1) // 检查 m_activePlot 是否为 null 并且索引有效
    {
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
            QMessageBox::information(this, tr("No Plot Selected"), tr("Please click on a plot to activate it before adding a signal."));
        }
        return;
    }

    // --- 使用 UniqueID ---
    QString uniqueID = item->data(UniqueIdRole).toString();
    QString signalName = item->text();
    if (uniqueID.isEmpty())
    {
        qWarning() << "Invalid signal item" << signalName;
        return;
    }

    // --- 使用新的辅助函数 ---
    if (item->checkState() == Qt::Checked)
    {
        qDebug() << "Adding signal" << signalName << "(id" << uniqueID << ") to plot" << m_activePlot;
        addSignalToPlot(uniqueID, m_activePlot);
    }
    else // Qt::Unchecked
    {
        qDebug() << "Removing signal" << signalName << "from plot" << m_activePlot;
        removeSignalFromPlot(uniqueID, m_activePlot);
    }
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

    // --- 3. 如果点击在预览线上，则打开新对话框 ---
    QString uniqueID = item->data(UniqueIdRole).toString();
    QPen currentPen = item->data(PenDataRole).value<QPen>();

    // --- 使用新的自定义对话框 ---
    SignalPropertiesDialog dialog(currentPen, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return; // 用户点击了 "Cancel"
    }

    QPen newPen = dialog.getSelectedPen(); // 获取包含所有属性的新 QPen

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

// 信号树的右键菜单槽 ---
void MainWindow::onSignalTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = m_signalTree->indexAt(pos);
    if (!index.isValid())
        return;

    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);
    // --- 只在文件条目上显示菜单 ---
    if (!item || !item->data(IsFileItemRole).toBool())
        return; // 只在文件条目上显示菜单

    QString filename = item->data(FileNameRole).toString(); // <-- 使用 FileNameRole

    QMenu contextMenu(this);
    QAction *deleteAction = contextMenu.addAction(tr("Remove '%1'").arg(filename));
    deleteAction->setData(filename); // 将文件名存储在动作中

    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteFileAction);
    contextMenu.exec(m_signalTree->viewport()->mapToGlobal(pos));
}

// 删除文件的动作 ---
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

// 移除文件的辅助函数 ---
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
        // --- 新的 ID 格式 ---
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
        // --- 确保我们得到的是顶层文件条目 ---
        if (item->data(IsFileItemRole).toBool() && item->parent() == nullptr)
        // --- --------------------------------- ---
        {
            m_signalTreeModel->removeRow(item->row());
            break; // 假设文件名是唯一的
        }
    }

    // 4. 清理和更新
    m_cursorManager->setupCursors();
    m_cursorManager->updateAllCursors();
    updateReplayManagerRange();

    on_actionFitView_triggered(); // 重新缩放视图
}

/**
 * @brief 响应重放按钮切换
 */
void MainWindow::onReplayActionToggled(bool checked)
{
    if (checked && m_cursorManager->getMode() == CursorManager::NoCursor)
    {
        // 如果没有游标，自动启用单游标
        m_cursorSingleAction->setChecked(true);
        // 手动触发 CursorManager 更新
        m_cursorManager->setMode(CursorManager::SingleCursor);
    }
}

/**
 * @brief  获取全局时间范围
 */
QCPRange MainWindow::getGlobalTimeRange() const
{
    // --- 遍历所有文件 ---
    if (m_fileDataMap.isEmpty())
        return QCPRange(0, 1);

    bool first = true;
    QCPRange totalRange;
    for (const FileData &data : m_fileDataMap.values())
    {
        // 遍历所有表 ---
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
    }

    if (first) // 意味着没有文件有数据
        return QCPRange(0, 1);
    else
        return totalRange;
}

/**
 * @brief 估算数据时间步
 */
double MainWindow::getSmallestTimeStep() const
{
    // --- 查找所有文件中的最小步长 ---
    double minStep = -1.0;

    for (const FileData &data : m_fileDataMap.values())
    {
        // 遍历所有表 ---
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
    }

    return (minStep > 0) ? minStep : 0.01; // 默认步长
}

/**
 * @brief 辅助函数，用于将数据范围推送到 ReplayManager
 */
void MainWindow::updateReplayManagerRange()
{
    if (m_replayManager)
    {
        m_replayManager->updateDataRange(getGlobalTimeRange(), getSmallestTimeStep());
    }
}

/**
 * @brief [槽] 适应视图大小 (所有子图, X 和 Y 轴)
 */
void MainWindow::on_actionFitView_triggered()
{
    if (m_plotWidgets.isEmpty())
        return;

    // --- 单独缩放每个图表的 Y 轴，但同步 X 轴 ---
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
}

/**
 * @brief [槽] 适应视图大小 (所有子图, 仅 X 轴)
 */
void MainWindow::on_actionFitViewTime_triggered()
{
    if (m_plotWidgets.isEmpty())
        return;

    // --- 找到全局 X 范围并应用 ---
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
}

/**
 * @brief [槽] 适应视图大小 (仅活动子图, 仅 Y 轴)
 */
void MainWindow::on_actionFitViewY_triggered()
{
    // --- 仅缩放当前X轴范围内的Y轴 ---
    if (m_activePlot && m_activePlot->graphCount() > 0)
    {
        QCPRange keyRange = m_activePlot->xAxis->range();
        QCPRange valueRange;
        bool foundRange = false;

        // 遍历活动子图上的所有图表
        // --- 使用 m_plotGraphMap ---
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
            // 添加5%的上下边距
            double size = valueRange.size();
            double margin = size * 0.05;
            valueRange.lower -= margin;
            valueRange.upper += margin;

            m_activePlot->yAxis->setRange(valueRange);
            m_activePlot->replot();
        }
    }
}

// X轴同步槽函数实现 ---
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
    if (m_cursorManager->getMode() != CursorManager::NoCursor)
    {
        m_cursorManager->updateAllCursors();
    }
}

// 辅助函数
/**
 * @brief  从 m_plotGraphMap 中安全地获取一个 QCPGraph*
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
 * @brief  从 QStandardItem 构建 UniqueID
 */
QString MainWindow::getUniqueID(QStandardItem *item) const
{
    if (!item || !item->data(IsSignalItemRole).toBool())
        return QString();

    // --- UniqueID 现在直接存储在条目中 ---
    return item->data(UniqueIdRole).toString();
}

/**
 * @brief [槽] 在布局更改和重绘完成后更新游标位置
 */
void MainWindow::updateCursorsForLayoutChange()
{
    if (m_cursorManager->getMode() != CursorManager::NoCursor)
    {
        m_cursorManager->updateAllCursors();
    }
}

// ---
// ---
// 图例交互槽函数
// ---
// ---

/**
 * @brief [槽] 响应图例左键点击，切换信号的可见性
 */
void MainWindow::onLegendClick(QCPLegend *legend, QCPAbstractLegendItem *item, QMouseEvent *event)
{
    Q_UNUSED(legend);
    if (event->button() != Qt::LeftButton) // 仅响应左键点击
        return;

    // 确保我们点击的是一个与 plottable 关联的图例条目
    if (QCPPlottableLegendItem *plottableItem = qobject_cast<QCPPlottableLegendItem *>(item))
    {
        QCPAbstractPlottable *plottable = plottableItem->plottable();
        if (plottable)
        {
            // 切换可见性
            plottable->setVisible(!plottable->visible());

            // QCustomPlot 会自动更新图例条目的外观（例如，变灰）
            // 我们只需要重绘图表
            plottable->parentPlot()->replot();
        }
    }
}

/**
 * @brief [槽] 响应图表区域的右键点击，检查是在图例上、图线上还是在图表背景上
 */
void MainWindow::onLegendContextMenu(const QPoint &pos)
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    // 首先检查是否点击了图线 (QCPGraph) ---
    // 我们使用 plottableAt 来查找鼠标位置下的 plottable
    // "false" 表示我们不关心它是否可选，我们只想知道它是否在那里
    QCPAbstractPlottable *plottable = plot->plottableAt(pos, false);
    QCPGraph *graph = qobject_cast<QCPGraph *>(plottable);

    if (graph)
    {
        // --- 1. 用户右键点击了 *图线* ---
        // 找到此 graph 对应的 uniqueID
        QString uniqueID = m_plotGraphMap.value(plot).key(graph, QString());

        if (uniqueID.isEmpty())
            return;

        // 创建上下文菜单
        QMenu contextMenu(this);
        QAction *deleteAction = contextMenu.addAction(tr("Delete '%1'").arg(graph->name()));
        deleteAction->setData(uniqueID); // 将 uniqueID 存储在 action 中

        connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSignalAction);

        // 在全局坐标位置显示菜单
        contextMenu.exec(plot->mapToGlobal(pos));
        return; // 处理完毕，退出函数
    }

    // --- 如果没有点击图线，则继续检查图例项或背景 ---

    // 检查点击位置的顶层可布局元素
    QCPLayoutElement *el = plot->layoutElementAt(pos);

    // 尝试将元素转换为图例条目
    QCPAbstractLegendItem *legendItem = qobject_cast<QCPAbstractLegendItem *>(el);

    if (QCPPlottableLegendItem *plottableItem = qobject_cast<QCPPlottableLegendItem *>(legendItem))
    {
        // --- 2. 用户右键点击了 *图例条目* ---
        graph = qobject_cast<QCPGraph *>(plottableItem->plottable()); // 复用 graph 变量
        if (!graph)
            return;

        // 找到此 graph 对应的 uniqueID
        QString uniqueID = m_plotGraphMap.value(plot).key(graph, QString());

        if (uniqueID.isEmpty())
            return;

        // 创建上下文菜单
        QMenu contextMenu(this);
        QAction *deleteAction = contextMenu.addAction(tr("Delete '%1'").arg(graph->name()));
        deleteAction->setData(uniqueID); // 将 uniqueID 存储在 action 中

        connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSignalAction);

        // 在全局坐标位置显示菜单
        contextMenu.exec(plot->mapToGlobal(pos));
    }
    else if (qobject_cast<QCPAxisRect *>(el) || qobject_cast<QCPLegend *>(el))
    {
        // --- 3. (不变) 用户右键点击了 *图表背景* (QCPAxisRect) 或 *图例背景* (QCPLegend) ---

        // 找到此 plot 对应的 plotIndex
        int plotIndex = m_plotWidgetMap.value(plot, -1);
        if (plotIndex == -1)
            return;

        QMenu contextMenu(this);
        QAction *deleteSubplotAction = contextMenu.addAction(tr("Delete Subplot"));
        deleteSubplotAction->setData(plotIndex); // 将 plotIndex 存储在 action 中

        connect(deleteSubplotAction, &QAction::triggered, this, &MainWindow::onDeleteSubplotAction);

        // 在全局坐标位置显示菜单
        contextMenu.exec(plot->mapToGlobal(pos));
    }
}

/**
 * @brief [槽] 响应图例上下文菜单中的“删除”动作
 */
void MainWindow::onDeleteSignalAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    QString uniqueID = action->data().toString();
    if (uniqueID.isEmpty())
        return;

    // --- 使用更可靠的 BFS 搜索替换 findItems 循环 ---
    QStandardItem *itemToUncheck = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);

    if (itemToUncheck)
    {
        itemToUncheck->setCheckState(Qt::Unchecked);
    }
    else
    {
        qWarning() << "onDeleteSignalAction: Could not find item in tree model for ID" << uniqueID;
    }
}

/**
 * @brief [槽] 响应子图上下文菜单中的“删除子图”动作
 * * 此函数通过取消勾选树中的所有相关条目来移除子图上的所有信号。
 */
void MainWindow::onDeleteSubplotAction()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    int plotIndex = action->data().toInt();
    if (!m_plotSignalMap.contains(plotIndex))
        return;

    // 重点：我们必须迭代一个 *副本*，
    // 因为取消勾选会触发 onSignalItemChanged，
    // 这将修改原始的 m_plotSignalMap[plotIndex]，
    // 从而使迭代器失效。
    const QSet<QString> signalIDsCopy = m_plotSignalMap.value(plotIndex);

    if (signalIDsCopy.isEmpty())
        return; // 子图上没有信号

    // 我们不在这里阻塞信号，因为我们 *希望* onSignalItemChanged
    // 为每个被取消勾选的条目触发，以正确执行所有清理逻辑。
    // QSignalBlocker blocker(m_signalTreeModel); // <-- 不要这样做

    qDebug() << "Clearing subplot index" << plotIndex << "- removing" << signalIDsCopy.size() << "signals.";

    for (const QString &uniqueID : signalIDsCopy)
    {
        // 查找树中的条目
        QStandardItem *itemToUncheck = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);

        // 如果找到了，并且它当前被选中，则取消勾选它
        if (itemToUncheck && itemToUncheck->checkState() == Qt::Checked)
        {
            itemToUncheck->setCheckState(Qt::Unchecked);
            // 这将自动调用 onSignalItemChanged(itemToUncheck)，
            // 该函数会移除图表、更新 map 并重绘。
        }
    }

    // 第一次重绘可能已经由最后一个 onSignalItemChanged 调用触发，
    // 但为保险起见，我们可以再次调用 replot (如果需要的话)。
    // 不过，onSignalItemChanged 已经处理了重绘，所以这里通常是多余的。
    // QCustomPlot *plot = m_plotWidgetMap.key(plotIndex, nullptr);
    // if (plot)
    //     plot->replot();
}

// ---
// ---
// 搜索过滤逻辑
// ---
// ---

/**
 * @brief [辅助函数] 递归地过滤信号树。
 * @param item 当前要检查的 QStandardItem
 * @param query 小写的搜索查询
 * @return true 如果此项或其任何子项匹配查询，则返回
 */
bool MainWindow::filterSignalTree(QStandardItem *item, const QString &query)
{
    if (!item)
        return false;

    // 1. 检查此项是否匹配
    // 我们匹配信号、文件和表名
    bool selfMatches = item->text().toLower().contains(query);

    // 2. 检查是否有任何子项匹配
    bool childrenMatch = false;
    for (int i = 0; i < item->rowCount(); ++i)
    {
        if (filterSignalTree(item->child(i), query))
        {
            childrenMatch = true;
        }
    }

    // 3. 决定可见性
    bool visible = selfMatches || childrenMatch;

    // 4. 如果查询为空，所有项都可见
    if (query.isEmpty())
    {
        visible = true;
    }

    // 5. 在视图中设置行隐藏
    // 根项没有父项，所以使用 QModelIndex()
    QModelIndex parentIndex = item->parent() ? item->parent()->index() : QModelIndex();
    m_signalTree->setRowHidden(item->row(), parentIndex, !visible);

    return visible;
}

/**
 * @brief [槽] 当信号搜索框中的文本更改时调用
 */
void MainWindow::onSignalSearchChanged(const QString &text)
{
    QString query = text.trimmed().toLower();
    QStandardItem *root = m_signalTreeModel->invisibleRootItem();

    // 递归地遍历所有项并设置它们的隐藏状态
    for (int i = 0; i < root->rowCount(); ++i)
    {
        filterSignalTree(root->child(i), query);
    }

    // 如果在搜索，展开所有内容以显示匹配项
    if (!query.isEmpty())
    {
        m_signalTree->expandAll();
    }
}

/**
 * @brief [槽] 当子图中的选择发生用户更改时调用
 */
void MainWindow::onPlotSelectionChanged()
{
    // 1. 获取是哪个 QCustomPlot 发出的信号
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    // 2. 获取该子图上当前选中的图表 (plottables)
    QList<QCPAbstractPlottable *> selected = plot->selectedPlottables(); //
    if (selected.isEmpty())
    {
        // 如果没有选中的图表 (例如，用户可能点击了空白处以取消所有选择)
        // 我们可以选择清除树中的当前索引
        m_signalTree->setCurrentIndex(QModelIndex());
        return;
    }

    // 3. 我们只关心第一个被选中的图表
    QCPGraph *graph = qobject_cast<QCPGraph *>(selected.first());
    if (!graph)
        return; // 选中的可能不是 QCPGraph

    // 4. 从我们的映射中反向查找该图表的 UniqueID
    // m_plotGraphMap 是 QMap<QCustomPlot *, QMap<QString, QCPGraph *>>
    // QMap::key() 可以高效地通过 value (QCPGraph*) 查找 key (QString)
    QString uniqueID = m_plotGraphMap.value(plot).key(graph, QString());
    if (uniqueID.isEmpty())
        return; // 未在映射中找到

    // 5. 在树模型中查找与该 ID 对应的项
    QStandardItem *item = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID); //
    if (!item)
        return;

    // 6. 滚动到该项并将其设置为当前选中项
    // (我们阻塞信号，以防 setCurrentIndex 触发不必要的重绘或逻辑)
    {
        QSignalBlocker blocker(m_signalTree);
        m_signalTree->scrollTo(item->index(), QAbstractItemView::PositionAtCenter);
        m_signalTree->setCurrentIndex(item->index()); //
    }
}

/**
 * @brief 切换所有子图中图例的可见性
 */
void MainWindow::on_actionToggleLegend_toggled(bool checked)
{
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot && plot->legend)
        {
            plot->legend->setVisible(checked);
            plot->replot();
        }
    }
}

// ---
// ---
// 事件过滤器
// ---
// ---

/**
 * @brief 事件过滤器，用于处理 QCustomPlot 上的拖放事件
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 1. 检查事件是否来自我们的某个 QCustomPlot 控件
    QCustomPlot *targetPlot = qobject_cast<QCustomPlot *>(watched);
    if (m_plotWidgets.contains(targetPlot))
    {
        // 2. 处理拖动进入事件 (DragEnter)
        if (event->type() == QEvent::DragEnter)
        {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent *>(event);

            // 检查 MimeData 是否来自 QStandardItemModel (即我们的树视图)
            if (dragEvent->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist"))
            {
                // 可选：更严格的检查，确保它是一个信号条目
                QByteArray encoded = dragEvent->mimeData()->data("application/x-qabstractitemmodeldatalist");
                QDataStream stream(&encoded, QIODevice::ReadOnly);
                if (!stream.atEnd())
                {
                    int r, c;
                    QMap<int, QVariant> data;
                    stream >> r >> c >> data; // 只读取第一个条目进行检查

                    // 如果它是一个信号条目 (而不是文件或表)，则接受拖动
                    if (data.contains(UniqueIdRole) && data.value(IsSignalItemRole).toBool())
                    {
                        dragEvent->acceptProposedAction();
                        return true; // 已处理事件
                    }
                }
            }
            // 如果 MimeData 不正确，则忽略事件 (event->ignore() 是默认的)
        }
        // 3. 处理放下事件 (Drop)
        else if (event->type() == QEvent::Drop)
        {
            QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
            QByteArray encoded = dropEvent->mimeData()->data("application/x-qabstractitemmodeldatalist");
            QDataStream stream(&encoded, QIODevice::ReadOnly);

            // 循环处理所有被拖拽的条目
            while (!stream.atEnd())
            {
                int r, c;
                QMap<int, QVariant> data;
                stream >> r >> c >> data; // 读取每个条目的数据

                if (data.contains(UniqueIdRole) && data.value(IsSignalItemRole).toBool())
                {
                    QString uniqueID = data.value(UniqueIdRole).toString();
                    QStandardItem *item = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);
                    if (!item)
                        continue;
                    int targetPlotIndex = m_plotWidgetMap.value(targetPlot, -1);
                    if (targetPlotIndex == -1)
                        continue;

                    bool alreadyOnPlot = m_plotSignalMap.value(targetPlotIndex).contains(uniqueID);

                    // 2. 如果不在，则添加它
                    if (!alreadyOnPlot)
                    {
                        // 设为活动子图 (这样 onSignalItemChanged/addSignalToPlot 会自动使用它)
                        setActivePlot(targetPlot);

                        if (item->checkState() == Qt::Unchecked)
                        {
                            // 设为勾选, 这将触发 onSignalItemChanged,
                            // 后者调用 addSignalToPlot
                            item->setCheckState(Qt::Checked);
                        }
                        else // item 已经是勾选状态
                        {
                            // onSignalItemChanged 不会触发,
                            // 我们必须手动调用 addSignalToPlot
                            addSignalToPlot(uniqueID, targetPlot);
                        }
                    }
                }
            }
            dropEvent->acceptProposedAction();

            // 拖放完成后清除树的选择 ---
            m_signalTree->clearSelection();

            updateSignalTreeChecks(); // 确保树的勾选状态在拖放后正确同步
            return true;              // 已处理事件
        }
    }

    // 4. 将所有其他事件传递给基类
    return QMainWindow::eventFilter(watched, event);
}

/**
 * @brief [槽] 当 OpenGL 动作被切换时调用
 * @param checked 动作的新勾选状态
 */
void MainWindow::onOpenGLActionToggled(bool checked)
{
    qDebug() << "Setting OpenGL acceleration to:" << checked;

    // 遍历所有当前存在的 QCustomPlot 实例并更新它们
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot)
        {
            plot->setOpenGl(checked);
            plot->replot(); // 立即重绘以应用更改
        }
    }
}