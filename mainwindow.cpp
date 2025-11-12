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
#include <QFormLayout>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QLineEdit>   // <-- 新增
#include <QVBoxLayout> // <-- 新增
// --- 新增：包含拖放和MIME数据的头文件 ---
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

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
    : QMainWindow(parent), // --- 修复：将所有指针成员初始化为 nullptr ---
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
      m_customLayoutDialog(nullptr), // <-- 崩溃修复
      m_customRowsSpinBox(nullptr),  // <-- 崩溃修复
      m_customColsSpinBox(nullptr),  // <-- 崩溃修复
      m_replayDock(nullptr),
      m_replayWidget(nullptr),
      m_playPauseButton(nullptr),
      m_stepForwardButton(nullptr),
      m_stepBackwardButton(nullptr),
      m_speedSpinBox(nullptr),
      m_timeSlider(nullptr),
      m_currentTimeLabel(nullptr),
      m_replayTimer(nullptr),
      // --- 非指针成员保持不变 ---
      m_cursorMode(NoCursor),
      m_cursorKey1(0),
      m_cursorKey2(0),
      m_isDraggingCursor1(false),
      m_isDraggingCursor2(false),
      m_colorIndex(0)
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

    // --- 新增：在构造函数中启用拖放 ---
    setAcceptDrops(true);

    // --- 新增：初始化颜色列表 ---
    // 默认颜色 (7)
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

    // --- 替换布局菜单 ---
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
    // --- ---------------- ---

    // 视图缩放动作
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon)); // 使用标准图标
    m_fitViewAction->setToolTip(tr("适应视图"));
    connect(m_fitViewAction, &QAction::triggered, this, &MainWindow::on_actionFitView_triggered);

    m_fitViewTimeAction = new QAction(tr("Fit View (Time)"), this);
    m_fitViewTimeAction->setIcon(QIcon::fromTheme("zoom-fit-width", style()->standardIcon(QStyle::SP_ArrowRight))); // 尝试主题图标
    m_fitViewTimeAction->setToolTip(tr("适应视图（时间轴）"));
    connect(m_fitViewTimeAction, &QAction::triggered, this, &MainWindow::on_actionFitViewTime_triggered);

    m_fitViewYAction = new QAction(tr("Fit View (Y-Axis)"), this);
    m_fitViewYAction->setIcon(QIcon::fromTheme("zoom-fit-height", style()->standardIcon(QStyle::SP_ArrowDown))); // 尝试主题图标
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
    connect(m_cursorGroup, &QActionGroup::triggered, this, &MainWindow::onCursorModeChanged);

    m_replayAction = new QAction(tr("重放"), this);
    m_replayAction->setCheckable(true);
    connect(m_replayAction, &QAction::toggled, this, &MainWindow::onReplayActionToggled);
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
    m_signalDock = new QDockWidget(tr("信号"), this);

    // --- 新增：创建一个容器 QWidget 来存放搜索框和树 ---
    QWidget *dockWidget = new QWidget(m_signalDock);
    QVBoxLayout *dockLayout = new QVBoxLayout(dockWidget);
    dockLayout->setContentsMargins(4, 4, 4, 4); // 紧凑边距
    dockLayout->setSpacing(4);                  // 控件间距

    // --- 新增：创建并添加搜索框 ---
    m_signalSearchBox = new QLineEdit(dockWidget);
    m_signalSearchBox->setPlaceholderText(tr("搜索信号..."));
    m_signalSearchBox->setClearButtonEnabled(true);
    dockLayout->addWidget(m_signalSearchBox);
    // --- ---------------------- ---

    m_signalTree = new QTreeView(dockWidget);
    m_signalTreeModel = new QStandardItemModel(m_signalDock);
    m_signalTree->setModel(m_signalTreeModel);
    m_signalTree->setHeaderHidden(true);
    m_signalTree->setItemDelegate(new SignalTreeDelegate(m_signalTree));

    dockLayout->addWidget(m_signalTree); // <-- 将树添加到布局中

    m_signalDock->setWidget(dockWidget); // <-- 设置容器 QWidget 为 dock 的控件

    // --- --------------------------------------------------- ---

    m_signalDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);

    connect(m_signalTreeModel, &QStandardItemModel::itemChanged, this, &MainWindow::onSignalItemChanged);
    connect(m_signalTree, &QTreeView::doubleClicked, this, &MainWindow::onSignalItemDoubleClicked);

    // --- 新增：连接右键菜单 ---
    m_signalTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onSignalTreeContextMenu);

    // --- 新增：连接搜索框信号 ---
    connect(m_signalSearchBox, &QLineEdit::textChanged, this, &MainWindow::onSignalSearchChanged);
    // --- ------------------------ ---
}

/**
 * @brief 创建底部重放停靠栏
 */
void MainWindow::createReplayDock()
{
    m_replayDock = new QDockWidget(tr("重放控制"), this);
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

    QFont axisFont = plot->font();           // 从绘图控件获取基础字体
    axisFont.setPointSize(7);                // 将字号设置为 9 (你可以按需调整)
    plot->xAxis->setTickLabelFont(axisFont); // X轴的刻度数字
    plot->xAxis->setLabelFont(axisFont);     // X轴的标签
    plot->yAxis->setTickLabelFont(axisFont); // Y轴的刻度数字
    plot->yAxis->setLabelFont(axisFont);     // Y轴的标签

    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);

    // --- 修改：连接新的鼠标事件处理器 ---
    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotMousePress);
    connect(plot, &QCustomPlot::mouseMove, this, &MainWindow::onPlotMouseMove);
    connect(plot, &QCustomPlot::mouseRelease, this, &MainWindow::onPlotMouseRelease);

    // X轴同步
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &MainWindow::onXAxisRangeChanged);

    // --- 新增：连接子图的选择信号，以同步树视图 ---
    connect(plot, &QCustomPlot::selectionChangedByUser, this, &MainWindow::onPlotSelectionChanged);

    // --- 新增：设置Y轴的数字格式 ---
    // (使用 'g' 格式并设置精度，以便大数字自动切换到科学计数法)
    plot->yAxis->setNumberFormat("g");  // 'g' = 通用格式
    plot->yAxis->setNumberPrecision(4); // 精度为 4 (例如 90000 -> 9e+4)

    // --- 新增：连接图例交互信号 ---

    // 1. (修正) 连接图例的左键点击信号，用于切换可见性
    //    这个信号在 QCustomPlot *plot* 上，而不是在 plot->legend 上
    // connect(plot, &QCustomPlot::legendClick, this, &MainWindow::onLegendClick);

    // 2. 启用并连接图表的上下文菜单（用于图例的右键点击）
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(plot, &QCustomPlot::customContextMenuRequested, this, &MainWindow::onLegendContextMenu);
    // --- -------------------------- ---
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

/**
 * @brief [新增] 核心布局函数，使用 QRect 列表创建网格
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

        // 强制设置固定的左边距以实现游标对齐
        plot->axisRect()->setAutoMargins(QCP::MarginSides(QCP::msAll & ~QCP::msLeft));
        QMargins newMargins = plot->axisRect()->margins();
        newMargins.setLeft(50); // <-- 你可以在这里调整这个固定值
        plot->axisRect()->setMargins(newMargins);

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
    setupCursors();

    if (m_cursorMode != NoCursor)
    {
        QTimer::singleShot(0, this, &MainWindow::updateCursorsForLayoutChange);
    }
}

/**
 * @brief [重构] 设置中央绘图区域的布局 (如 2x2)
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
        m_customRowsSpinBox->setValue(2);

        m_customColsSpinBox = new QSpinBox(m_customLayoutDialog);
        m_customColsSpinBox->setRange(1, 8);
        m_customColsSpinBox->setValue(2);

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
 * @brief [新增] 启动加载单个文件的辅助函数
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

// --- 新增：拖放事件实现 ---

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
// --- ---------------------- ---

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

    // --- 新增：默认展开所有条目 ---
    m_signalTree->expandAll();

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

            if (m_colorList.isEmpty()) // 安全检查
            {
                m_colorList << Qt::black;
            }

            QColor color = m_colorList.at(m_colorIndex);
            m_colorIndex = (m_colorIndex + 1) % m_colorList.size();

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

        // ---
        // --- 新增修复：优化选中性能 ---
        // ---
        // 默认的 QCPSelectionDecorator 使用 2.5px 宽的笔。
        // 在绘制密集数据时，这会导致严重的性能问题并使UI冻结。
        // 我们通过修改装饰器来解决这个问题，使其使用与图表
        // 原始线条 *相同宽度* 的笔，只改变颜色。
        if (graph->selectionDecorator())
        {
            QCPSelectionDecorator *decorator = graph->selectionDecorator(); //
            QPen selPen = decorator->pen();                                 // 获取默认的选中笔刷（蓝色，2.5px）

            // 将宽度修改为与原始线条相同（在我们的例子中为 1px）
            selPen.setWidth(pen.width());

            decorator->setPen(selPen);

            // 我们不希望选中时出现填充或更改散点样式，
            // 所以我们明确禁用它们（尽管它们默认可能是关闭的）
            decorator->setBrush(Qt::NoBrush);
            decorator->setUsedScatterProperties(QCPScatterStyle::spNone); //
        }
        // --- 修复结束 ---

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

    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    if (event->button() == Qt::LeftButton)
    {
        // 检查用户是否按下了多选键 (例如 Ctrl)
        // 我们从 plot 实例中获取多选修饰键的设置
        bool multiSelect = (event->modifiers() & plot->multiSelectModifier());

        // 如果没有按下多选键，则执行“点击取消所有选中”
        if (!multiSelect)
        {
            bool selectionChanged = false;
            // 遍历所有图表，取消它们的选中状态
            for (QCustomPlot *p : m_plotWidgets)
            {
                // 检查是否有任何内容被选中
                if (!p->selectedPlottables().isEmpty() || !p->selectedGraphs().isEmpty() || !p->selectedItems().isEmpty() || !p->selectedAxes().isEmpty() || !p->selectedLegends().isEmpty())
                {
                    selectionChanged = true;
                }
                p->deselectAll(); // 取消此 QCustomPlot 实例上的所有选中
            }

            // 如果确实有选中状态被改变，我们需要重绘所有图表
            // (deselectAll 不会自动触发重绘)
            if (selectionChanged)
            {
                for (QCustomPlot *p : m_plotWidgets)
                {
                    // 使用排队重绘，以防万一
                    p->replot(QCustomPlot::rpQueuedReplot);
                }
            }
        }
    }
    // --- 新增代码结束 ---

    if (m_cursorMode == NoCursor)
        return; // 游标未激活

    // QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender()); // <-- 这行已移到新增代码的开头
    // if (!plot)
    //     return;

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
    // (我们新增的取消选中逻辑已经在此之前运行了)
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

    // 1. 从 plot 获取平滑的 x 坐标 (key)
    double smoothKey = plot->xAxis->pixelToCoord(event->pos().x());
    double snappedKey = smoothKey; // 默认使用平滑键

    // --- 拖拽逻辑 (在此处实现吸附) ---
    if (m_isDraggingCursor1 || m_isDraggingCursor2)
    {
        // 仅在拖动时执行吸附
        double closestKey = smoothKey;
        double minDistance = -1.0;

        // 2. 查找此图(plot)上的所有图表(graph)
        const auto &graphsOnPlot = m_plotGraphMap.value(plot);
        if (!graphsOnPlot.isEmpty())
        {
            // 3. 遍历此图上的所有图表，找到最近的数据点键
            for (QCPGraph *graph : graphsOnPlot)
            {
                if (graph && !graph->data()->isEmpty())
                {
                    // 使用 QCPDataContainer 的 findBegin 进行高效的二分查找
                    auto it = graph->data()->findBegin(smoothKey); // 找到第一个 >= smoothKey 的点

                    // 检查找到的点
                    if (it != graph->data()->constEnd())
                    {
                        double distAt = qAbs(it->key - smoothKey);
                        if (minDistance < 0 || distAt < minDistance)
                        {
                            minDistance = distAt;
                            closestKey = it->key;
                        }
                    }

                    // 检查找到的点的前一个点
                    if (it != graph->data()->constBegin())
                    {
                        double distBefore = qAbs((it - 1)->key - smoothKey);
                        if (minDistance < 0 || distBefore < minDistance)
                        {
                            minDistance = distBefore;
                            closestKey = (it - 1)->key;
                        }
                    }
                }
            } // 结束 for (graphs)

            if (minDistance >= 0) // 如果找到了一个最近的键
            {
                snappedKey = closestKey; // 4. 使用吸附后的键
            }
        }
        // else: 如果此图上没有图表, 将使用 smoothKey
    }

    // --- 5. 使用最终的键 (snappedKey) 更新游标 ---
    if (m_isDraggingCursor1)
    {
        updateCursors(snappedKey, 1);
        event->accept();
    }
    else if (m_isDraggingCursor2)
    {
        updateCursors(snappedKey, 2);
        event->accept();
    }
    // --- 悬停逻辑 (保持不变，不吸附) ---
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
            // 悬停检查是基于像素位置的，所以我们不需要使用键
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

    // 为X轴游标标签创建小号字体-
    QFont cursorFont;
    if (!m_plotWidgets.isEmpty())
        cursorFont = m_plotWidgets.first()->font(); // 使用第一个图表的字体作为基础
    else
        cursorFont = this->font(); // 后备为窗口字体

    cursorFont.setPointSize(7); // 设置为你想要的小字号, 比如 9

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
        xLabel1->setFont(cursorFont);
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
            xLabel2->setFont(cursorFont);
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

// ---
// ---
// --- 新增：图例交互槽函数
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

    // --- 新增：首先检查是否点击了图线 (QCPGraph) ---
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
    // --- 新增逻辑结束 ---

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

    // --- 修正：使用更可靠的 BFS 搜索替换 findItems 循环 ---
    QStandardItem *itemToUncheck = findItemByUniqueID_BFS(m_signalTreeModel, uniqueID);
    // --- ------------------------------------------- ---

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
    // 这将 *修改* 原始的 m_plotSignalMap[plotIndex]，
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
// --- 新增：搜索过滤逻辑
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