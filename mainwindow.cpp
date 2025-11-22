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
#include <QDomDocument>
#include <QColor>
#include <QMap>
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"

// 自定义流式布局图例类
class FlowLegend : public QCPLegend
{
public:
    explicit FlowLegend() : QCPLegend()
    {
        setColumnSpacing(10);
        setRowSpacing(5);
    }

    virtual QSize minimumOuterSizeHint() const override
    {
        if (itemCount() == 0)
            return QSize(0, 0);

        int currentWidth = mOuterRect.width();
        if (currentWidth <= 0)
            currentWidth = 100;

        // 只计算高度，不应用布局 (apply=false)
        int requiredHeight = calculateLayout(currentWidth, false);

        return QSize(0, requiredHeight);
    }

    virtual void updateLayout() override
    {
        // 计算并应用布局 (apply=true)
        calculateLayout(mOuterRect.width(), true);
    }

private:
    // [核心逻辑] 统一的布局计算函数
    // apply: 如果为 true，则实际设置元素位置；如果为 false，仅计算所需高度
    int calculateLayout(int availWidth, bool apply) const
    {
        double x = mMargins.left();
        double y = mMargins.top();
        double currentLineHeight = 0;

        QList<QCPLayoutElement *> items;
        for (int i = 0; i < itemCount(); ++i)
        {
            if (QCPAbstractLegendItem *el = item(i))
            {
                if (el->realVisibility() && !el->minimumOuterSizeHint().isEmpty())
                    items.append(el);
            }
        }

        if (items.isEmpty())
            return mMargins.top() + mMargins.bottom();

        for (QCPLayoutElement *el : items)
        {
            QSize sz = el->minimumOuterSizeHint();

            // 换行判断
            if (x + sz.width() + mMargins.right() > availWidth && x > mMargins.left())
            {
                x = mMargins.left();
                y += currentLineHeight + mRowSpacing;
                currentLineHeight = 0;
            }

            // 应用位置
            if (apply)
            {
                el->setOuterRect(QRect(mOuterRect.left() + x, mOuterRect.top() + y, sz.width(), sz.height()));
            }

            x += sz.width() + mColumnSpacing;

            if (sz.height() > currentLineHeight)
                currentLineHeight = sz.height();
        }

        return y + currentLineHeight + mMargins.bottom();
    }
};

/**
 * @brief [辅助函数] 递归地在 QStandardItemModel 中按名称查找信号条目
 * @param parentItem 开始搜索的父项 (初始调用时传入 invisibleRootItem)
 * @param name 要查找的信号名称 (item->text())
 * @return 找到的 QStandardItem，如果未找到则返回 nullptr
 */
static QStandardItem *findItemByName_Recursive(QStandardItem *parentItem, const QString &name)
{
    if (!parentItem)
        return nullptr;

    for (int r = 0; r < parentItem->rowCount(); ++r)
    {
        QStandardItem *child = parentItem->child(r);
        if (!child)
            continue;

        if (child->data(IsSignalItemRole).toBool())
        {
            // 是信号条目，检查名称
            if (child->text() == name)
            {
                return child;
            }
        }
        else
        {
            // 是文件或表条目，递归搜索其子项
            QStandardItem *found = findItemByName_Recursive(child, name);
            if (found)
                return found;
        }
    }
    return nullptr;
}

static bool isSupportedFile(const QString &filePath)
{
    return filePath.endsWith(".csv", Qt::CaseInsensitive) ||
           filePath.endsWith(".txt", Qt::CaseInsensitive) ||
           filePath.endsWith(".mat", Qt::CaseInsensitive) ||
           filePath.endsWith(".mldatx", Qt::CaseInsensitive);
}

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
      m_importViewAction(nullptr),
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
      m_exportAllAction(nullptr),
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

    m_cursorManager = new CursorManager(&m_plotWidgets, this);

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

    m_exportAllAction = new QAction(tr("Export All Views..."), this);
    m_exportAllAction->setStatusTip(tr("Export the entire view layout as an image"));
    m_exportAllAction->setShortcut(QKeySequence(tr("Ctrl+E")));
    connect(m_exportAllAction, &QAction::triggered, this, &MainWindow::on_actionExportAll_triggered);

    // 导入视图动作
    m_importViewAction = new QAction(tr("&Import View..."), this);
    connect(m_importViewAction, &QAction::triggered, this, &MainWindow::on_actionImportView_triggered);

    // 替换布局菜单
    m_layout1x1Action = new QAction(tr("1x1 Layout"), this);
    m_layout1x1Action->setData(QPoint(1, 1));

    m_layout1x2Action = new QAction(tr("1x2 Layout (Side by Side)"), this);
    m_layout1x2Action->setData(QPoint(1, 2));

    m_layout2x1Action = new QAction(tr("2x1 Layout (Stacked)"), this);
    m_layout2x1Action->setData(QPoint(2, 1));

    m_layout2x2Action = new QAction(tr("2x2 Layout"), this);
    m_layout2x2Action->setData(QPoint(2, 2));

    // 对于复杂的 Split 布局，我们使用字符串或特殊 ID 作为 Data
    m_layoutSplitBottomAction = new QAction(tr("Bottom Split"), this);
    m_layoutSplitBottomAction->setData("split_bottom");

    m_layoutSplitTopAction = new QAction(tr("Top Split"), this);
    m_layoutSplitTopAction->setData("split_top");

    m_layoutSplitLeftAction = new QAction(tr("Left Split"), this);
    m_layoutSplitLeftAction->setData("split_left");

    m_layoutSplitRightAction = new QAction(tr("Right Split"), this);
    m_layoutSplitRightAction->setData("split_right");

    QList<QAction *> layoutActions = {
        m_layout1x1Action, m_layout1x2Action, m_layout2x1Action, m_layout2x2Action,
        m_layoutSplitBottomAction, m_layoutSplitTopAction,
        m_layoutSplitLeftAction, m_layoutSplitRightAction};

    for (QAction *action : layoutActions)
    {
        connect(action, &QAction::triggered, this, &MainWindow::onLayoutActionTriggered);
    }

    m_layoutCustomAction = new QAction(tr("Custom Grid..."), this);
    connect(m_layoutCustomAction, &QAction::triggered, this, &MainWindow::on_actionLayoutCustom_triggered);

    // 视图缩放动作
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    m_fitViewAction->setToolTip(tr("适应视图"));
    m_fitViewAction->setShortcut(Qt::Key_Space);
    connect(m_fitViewAction, &QAction::triggered, this, &MainWindow::on_actionFitView_triggered);

    m_fitViewTimeAction = new QAction(tr("Fit View (Time)"), this);
    m_fitViewTimeAction->setIcon(QIcon::fromTheme("zoom-fit-width", style()->standardIcon(QStyle::SP_ArrowRight)));
    m_fitViewTimeAction->setToolTip(tr("适应视图（时间轴）"));
    m_fitViewTimeAction->setShortcut(QKeySequence(tr("Ctrl+Alt+T")));
    connect(m_fitViewTimeAction, &QAction::triggered, this, &MainWindow::on_actionFitViewTime_triggered);

    m_fitViewYAction = new QAction(tr("Fit View (Y-Axis)"), this);
    m_fitViewYAction->setIcon(QIcon::fromTheme("zoom-fit-height", style()->standardIcon(QStyle::SP_ArrowDown)));
    m_fitViewYAction->setToolTip(tr("适应视图（Y轴）"));
    m_fitViewYAction->setShortcut(QKeySequence(tr("Ctrl+Alt+Y")));
    connect(m_fitViewYAction, &QAction::triggered, this, &MainWindow::on_actionFitViewY_triggered);

    m_fitViewYAllAction = new QAction(tr("Fit View All (Y-Axis)"), this);
    m_fitViewYAllAction->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_fitViewYAllAction->setToolTip(tr("适应所有子图视图（Y轴）"));
    m_fitViewYAllAction->setShortcut(QKeySequence(tr("Ctrl+Shift+Y"))); // 设置快捷键 Ctrl+Shift+Y
    connect(m_fitViewYAllAction, &QAction::triggered, this, &MainWindow::on_actionFitViewYAll_triggered);

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
    m_toggleLegendAction->setChecked(true);
    m_toggleLegendAction->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    m_toggleLegendAction->setToolTip(tr("显示/隐藏图例"));
    connect(m_toggleLegendAction, &QAction::toggled, this, &MainWindow::on_actionToggleLegend_toggled);

    m_legendPosGroup = new QActionGroup(this);
    m_legendPosOutsideTopAction = new QAction(tr("图表外上方"), this);
    m_legendPosOutsideTopAction->setCheckable(true);
    m_legendPosOutsideTopAction->setData(0);
    m_legendPosOutsideTopAction->setChecked(true); // 默认选中

    m_legendPosInsideTLAction = new QAction(tr("图表内左上"), this);
    m_legendPosInsideTLAction->setCheckable(true);
    m_legendPosInsideTLAction->setData(1);

    m_legendPosInsideTRAction = new QAction(tr("图表内右上"), this);
    m_legendPosInsideTRAction->setCheckable(true);
    m_legendPosInsideTRAction->setData(2);
    m_legendPosGroup->addAction(m_legendPosOutsideTopAction);
    m_legendPosGroup->addAction(m_legendPosInsideTLAction);
    m_legendPosGroup->addAction(m_legendPosInsideTRAction);

    connect(m_legendPosGroup, &QActionGroup::triggered, this, &MainWindow::onLegendPositionChanged);

    // 创建 OpenGL 动作
    m_openGLAction = new QAction(tr("启用 OpenGL 加速"), this);
    m_openGLAction->setToolTip(tr("切换 QCustomPlot 的 OpenGL 渲染。"));
    m_openGLAction->setCheckable(true);
    m_openGLAction->setChecked(false); // 默认关闭
    connect(m_openGLAction, &QAction::toggled, this, &MainWindow::onOpenGLActionToggled);

    m_clearAllPlotsAction = new QAction(tr("Clear All Plots"), this);
    m_clearAllPlotsAction->setToolTip(tr("Remove all signals from all plots"));
    m_clearAllPlotsAction->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_clearAllPlotsAction->setShortcut(QKeySequence(tr("Ctrl+D")));
    connect(m_clearAllPlotsAction, &QAction::triggered, this, &MainWindow::on_actionClearAllPlots_triggered);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&文件"));
    fileMenu->addAction(m_loadFileAction);
    fileMenu->addAction(m_importViewAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAllAction);

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
    viewMenu->addAction(m_fitViewYAllAction);

    // 添加图例切换菜单项
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleLegendAction);

    // 添加图例位置子菜单
    QMenu *legendPosMenu = viewMenu->addMenu(tr("图例位置"));
    legendPosMenu->addAction(m_legendPosOutsideTopAction);
    legendPosMenu->addAction(m_legendPosInsideTLAction);
    legendPosMenu->addAction(m_legendPosInsideTRAction);

    // 创建 "设置" 菜单
    QMenu *settingsMenu = menuBar()->addMenu(tr("&设置"));
    settingsMenu->addAction(m_openGLAction);
}

void MainWindow::createToolBars()
{
    m_viewToolBar = new QToolBar(tr("View Toolbar"), this);

    QWidget *spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_viewToolBar->addWidget(spacer);

    // 添加视图缩放按钮
    m_viewToolBar->addAction(m_fitViewAction);
    m_viewToolBar->addAction(m_fitViewTimeAction);
    m_viewToolBar->addAction(m_fitViewYAction);
    m_viewToolBar->addAction(m_fitViewYAllAction);
    m_viewToolBar->addSeparator();

    // 添加图例切换按钮
    m_viewToolBar->addAction(m_toggleLegendAction);
    // 添加清除按钮到工具栏
    m_viewToolBar->addAction(m_clearAllPlotsAction);
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

    // 设置缩进宽度，默认通常为20，这里减小为10以减少层级缩进感
    m_signalTree->setIndentation(8);

    // 启用从树状视图拖动
    m_signalTree->setDragEnabled(true);
    m_signalTree->setDragDropMode(QAbstractItemView::DragOnly);
    m_signalTree->setSelectionMode(QAbstractItemView::ExtendedSelection); // 允许选择多行进行拖拽

    dockLayout->addWidget(m_signalTree); // 将树添加到布局中

    m_signalDock->setWidget(dockWidget); // 设置容器 QWidget 为 dock 的控件

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

void MainWindow::setupPlotLayout(const QList<QRect> &geometries)
{
    clearPlotLayout();

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotContainer->layout());
    if (!grid)
        grid = new QGridLayout(m_plotContainer);

    // 设置边距
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);

    QCPRange sharedXRange;
    bool hasSharedXRange = false;

    // 1. 创建组件
    for (int i = 0; i < geometries.size(); ++i)
    {
        const QRect &geo = geometries[i];
        int plotIndex = i;

        QFrame *plotFrame = new QFrame(m_plotContainer);
        plotFrame->setFrameShape(QFrame::NoFrame);
        plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }"); // 使用样式表管理边框

        QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
        frameLayout->setContentsMargins(0, 0, 0, 0);

        QCustomPlot *plot = new QCustomPlot(plotFrame);
        if (i == 0)
            m_yAxisGroup = new QCPMarginGroup(plot);

        frameLayout->addWidget(plot);
        grid->addWidget(plotFrame, geo.y(), geo.x(), geo.height(), geo.width());
        m_plotWidgets.append(plot);

        // 2. 恢复信号 (持久化数据)
        if (m_plotSignalMap.contains(plotIndex))
        {
            const QSet<QString> &signalIDs = m_plotSignalMap.value(plotIndex);
            for (const QString &uniqueID : signalIDs)
            {
                SignalLocation loc = getSignalDataFromID(uniqueID);
                if (loc.table)
                {
                    setupGraphInstance(plot, uniqueID, loc);
                }
            }

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
        }
        else if (hasSharedXRange)
        {
            plot->xAxis->setRange(sharedXRange);
        }

        setupPlotInteractions(plot);
    }

    // 恢复活动状态
    if (!m_plotWidgets.isEmpty())
    {
        setActivePlot(m_plotWidgets.first());
    }

    QTimer::singleShot(0, this, &MainWindow::updateCursorsForLayoutChange);
}

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

    // 检查文件类型
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

void MainWindow::importView(const QString &mldatxFilePath)
{
    if (mldatxFilePath.isEmpty())
        return;

    // 使用 QuaZip 打开
    QuaZip zip(mldatxFilePath);
    if (!zip.open(QuaZip::mdUnzip))
    {
        QMessageBox::critical(this, tr("Import Error"), tr("Error: Could not open file as ZIP archive."));
        return;
    }

    // 查找并解析关键 XML
    QDomDocument viewMetaDataDoc;
    QDomDocument checkedSignalsDoc;
    bool foundViewMeta = false;
    bool foundCheckedSignals = false;

    QStringList allFiles = zip.getFileNameList();
    qDebug() << "Found" << allFiles.size() << "files in archive. Looking for view XMLs...";

    for (const QString &fileName : allFiles)
    {
        // 我们只关心目标文件
        if (fileName != "views/sdi_view_meta_data.xml" && fileName != "views/sdi_checked_signals.xml")
        {
            continue;
        }

        if (!zip.setCurrentFile(fileName))
            continue;

        QuaZipFile zFile(&zip);
        if (!zFile.open(QIODevice::ReadOnly))
            continue;

        QByteArray xmlData = zFile.readAll();
        zFile.close();

        QDomDocument doc;
        QString errorMsg;
        int errorLine, errorCol;
        if (doc.setContent(xmlData, &errorMsg, &errorLine, &errorCol))
        {
            qDebug() << "  [Success] Parsed:" << fileName;
            if (fileName == "views/sdi_view_meta_data.xml")
            {
                viewMetaDataDoc = doc;
                foundViewMeta = true;
            }
            else if (fileName == "views/sdi_checked_signals.xml")
            {
                checkedSignalsDoc = doc;
                foundCheckedSignals = true;
            }
        }
        else
        {
            qWarning() << "  [Failed] Could not parse XML:" << fileName << "Error:" << errorMsg << "at line" << errorLine;
        }
    }
    zip.close();

    if (!foundViewMeta)
    {
        QMessageBox::critical(this, tr("Import Error"), tr("Error: Did not find 'views/sdi_view_meta_data.xml' in .mldatx file."));
        return;
    }
    if (!foundCheckedSignals)
    {
        QMessageBox::critical(this, tr("Import Error"), tr("Error: Did not find 'views/sdi_checked_signals.xml' in .mldatx file."));
        return;
    }

    qDebug() << "  Parsing Results  ";

    LayoutInfo layout = parseViewMetaData(viewMetaDataDoc);
    QList<SignalInfo> signalList = parseCheckedSignals(checkedSignalsDoc);

    qDebug().noquote() << QString("Layout Info: %1x%2 %3").arg(layout.rows).arg(layout.cols).arg(layout.layoutType);
    qDebug().noquote() << QString("Signal Info: Found %1 signals").arg(signalList.count());

    // 5. 应用布局
    applyImportedView(layout, signalList);
}

/**
 * @brief [槽] "导入视图..." 菜单动作被触发
 */
void MainWindow::on_actionImportView_triggered()
{
    QString mldatxFilePath = QFileDialog::getOpenFileName(this,
                                                          tr("Import Simulink View"), "", tr("Simulink Data (*.mldatx)"));

    if (mldatxFilePath.isEmpty())
        return;

    importView(mldatxFilePath);
}

void MainWindow::setupPlotInteractions(QCustomPlot *plot)
{
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables | QCP::iSelectLegend);

    plot->setAutoAddPlottableToLegend(false);

    int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;
    configurePlotLegend(plot, legendMode);

    if (plot->legend)
    {
        plot->legend->setSelectableParts(QCPLegend::spItems);
    }

    // 根据 m_openGLAction 的状态设置 OpenGL
    plot->setOpenGl(m_openGLAction->isChecked());

    QFont axisFont = plot->font();           // 从绘图控件获取基础字体
    axisFont.setPointSize(7);                // 将字号设置为 7
    plot->xAxis->setTickLabelFont(axisFont); // X轴的刻度数字
    plot->xAxis->setLabelFont(axisFont);     // X轴的标签
    plot->yAxis->setTickLabelFont(axisFont); // Y轴的刻度数字
    plot->yAxis->setLabelFont(axisFont);     // Y轴的标签

    // 将图例字体也设置为 7pt
    plot->legend->setFont(axisFont);
    plot->legend->setIconSize(10, 10);   // 将图标宽度设为20，高度设为10
    plot->legend->setIconTextPadding(3); // 将图标和文本的间距设为 3 像素

    // 使用 QCPMarginGroup 进行自动对齐
    plot->axisRect()->setMarginGroup(QCP::msLeft, m_yAxisGroup);

    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);

    // 连接新的鼠标事件处理器
    connect(plot, &QCustomPlot::mousePress, m_cursorManager, &CursorManager::onPlotMousePress);
    connect(plot, &QCustomPlot::mouseMove, m_cursorManager, &CursorManager::onPlotMouseMove);
    connect(plot, &QCustomPlot::mouseRelease, m_cursorManager, &CursorManager::onPlotMouseRelease);

    // X轴同步
    connect(plot->xAxis, static_cast<void (QCPAxis::*)(const QCPRange &)>(&QCPAxis::rangeChanged),
            this, &MainWindow::onXAxisRangeChanged);

    // 连接子图的选择信号
    connect(plot, &QCustomPlot::selectionChangedByUser, this, &MainWindow::onPlotSelectionChanged);

    // 设置Y轴的数字格式
    plot->yAxis->setNumberFormat("g");
    plot->yAxis->setNumberPrecision(4);

    // 允许子图接收拖放并安装事件过滤器
    plot->setAcceptDrops(true);
    plot->installEventFilter(this);

    // 连接图例交互信号
    plot->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(plot, &QCustomPlot::customContextMenuRequested, this, &MainWindow::onLegendContextMenu);
}

void MainWindow::clearPlotLayout()
{
    m_cursorManager->clearCursors();

    if (m_yAxisGroup)
    {
        delete m_yAxisGroup;
        m_yAxisGroup = nullptr;
    }

    // 清理布局
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
    m_lastMousePlot = nullptr;
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

    m_fileDataMap.insert(filename, data);

    //  填充信号树
    {
        QSignalBlocker blocker(m_signalTreeModel);
        populateSignalTree(data);
    }

    m_signalTree->reset();

    // 默认展开所有条目
    m_signalTree->expandAll();

    updateReplayManagerRange();
}

void MainWindow::onDataLoadFailed(const QString &filePath, const QString &errorString)
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Load Error"), tr("Failed to load %1:\n%2").arg(filePath).arg(errorString));
}

// 移除文件的辅助函数
void MainWindow::removeFile(const QString &filename)
{
    if (!m_fileDataMap.remove(filename))
        return;

    m_cursorManager->clearCursors();

    QString prefix = filename + "/";
    bool anyPlotChanged = false;

    // 1. 批量清理子图
    for (int i = 0; i < m_plotWidgets.size(); ++i)
    {
        QCustomPlot *plot = m_plotWidgets.at(i);
        QSet<QString> &signalSet = m_plotSignalMap[i];

        QList<QCPGraph *> graphsToDelete;
        for (int j = 0; j < plot->graphCount(); ++j)
        {
            QCPGraph *graph = plot->graph(j);
            QString graphID = graph->property("id").toString();
            if (graphID.startsWith(prefix))
            {
                graphsToDelete.append(graph);
                signalSet.remove(graphID);
            }
        }

        if (!graphsToDelete.isEmpty())
        {
            for (QCPGraph *g : graphsToDelete)
                plot->removeGraph(g);

            int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;
            configurePlotLegend(plot, legendMode);
            plot->replot();
            anyPlotChanged = true;
        }
    }

    // 2. 清理内部ID映射
    QMutableHashIterator<QString, QStandardItem *> it(m_uniqueIdMap);
    while (it.hasNext())
    {
        it.next();
        if (it.key().startsWith(prefix))
            it.remove();
    }

    // 3. 移除文件节点
    QList<QStandardItem *> items = m_signalTreeModel->findItems(filename);
    for (QStandardItem *item : items)
    {
        if (item->data(IsFileItemRole).toBool() && item->parent() == nullptr)
        {
            m_signalTreeModel->removeRow(item->row());
            break;
        }
    }

    if (anyPlotChanged)
    {
        m_cursorManager->setupCursors();
        m_cursorManager->updateAllCursors();
        updateReplayManagerRange();
        on_actionFitView_triggered();
    }
}

// 删除文件的动作
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

/**
 * @brief 将指定ID的信号添加到指定的子图中
 * @param uniqueID 要添加的信号ID
 * @param plot 目标 QCustomPlot
 */
void MainWindow::addSignalToPlot(const QString &uniqueID, QCustomPlot *plot, bool replot)
{
    int plotIndex = m_plotWidgets.indexOf(plot);
    if (plotIndex == -1)
        return;

    if (m_plotSignalMap.value(plotIndex).contains(uniqueID))
        return;

    SignalLocation loc = getSignalDataFromID(uniqueID);
    if (!loc.table || loc.signalIndex < 0 || loc.signalIndex >= loc.table->valueData.size())
        return;

    setupGraphInstance(plot, uniqueID, loc);

    // 更新映射
    m_plotSignalMap[plotIndex].insert(uniqueID);

    // 仅在需要时刷新
    if (replot)
    {
        plot->rescaleAxes();

        int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;
        configurePlotLegend(plot, legendMode);

        plot->replot();

        if (m_cursorManager->getMode() != CursorManager::NoCursor)
        {
            m_cursorManager->setupCursors(); // 暂时保留，作为必须的权衡，但应考虑优化 CursorManager
            m_cursorManager->updateAllCursors();
        }
    }
}

/**
 * @brief 从指定的子图中移除指定ID的信号
 * @param uniqueID 要移除的信号ID
 * @param plot 目标 QCustomPlot
 */
void MainWindow::removeSignalFromPlot(const QString &uniqueID, QCustomPlot *plot)
{
    int plotIndex = m_plotWidgets.indexOf(plot);
    if (plotIndex == -1)
        return;

    if (!m_plotSignalMap.value(plotIndex).contains(uniqueID))
    {
        return;
    }

    QCPGraph *graph = getGraph(plot, uniqueID);
    if (graph)
    {
        m_cursorManager->clearCursors();

        plot->removeGraph(graph);

        m_plotSignalMap[plotIndex].remove(uniqueID);

        int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;
        configurePlotLegend(plot, legendMode);

        plot->replot();
        m_cursorManager->setupCursors();
        m_cursorManager->updateAllCursors();
    }
}

/**
 * @brief 设置活动子图的辅助函数
 * @param plot 要激活的子图
 */
void MainWindow::setActivePlot(QCustomPlot *plot)
{
    if (!plot)
        return;

    // 取消高亮旧的 active plot
    if (m_activePlot)
    {
        if (QWidget *parent = m_activePlot->parentWidget())
            parent->setStyleSheet("QFrame { border: 2px solid transparent; }");
    }

    m_activePlot = plot;
    m_cursorManager->setActivePlot(plot);

    // 高亮新的 active plot
    if (QWidget *parent = m_activePlot->parentWidget())
    {
        parent->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
    }

    m_lastMousePlot = plot;

    updateSignalTreeChecks();
    m_signalTree->viewport()->update();
}

void MainWindow::onPlotClicked()
{
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot)
        return;

    // 仅当没点中图线时才取消选中，避免误触
    QPoint pos = clickedPlot->mapFromGlobal(QCursor::pos());
    if (!clickedPlot->plottableAt(pos, true))
    {
        clickedPlot->deselectAll();
        clickedPlot->replot();
    }

    // 切换选中样式
    if (m_activePlot && clickedPlot != m_activePlot)
    {
        m_activePlot->deselectAll();
        m_activePlot->replot();
    }

    setActivePlot(clickedPlot);
}

/**
 * @brief [槽] 当 OpenGL 动作被切换时调用
 * @param checked 动作的新勾选状态
 */
void MainWindow::onOpenGLActionToggled(bool checked)
{
    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (plot)
        {
            plot->setOpenGl(checked);
            plot->replot();
        }
    }
}

/**
 * @brief [槽] 当子图中的选择发生用户更改时调用
 */
void MainWindow::onPlotSelectionChanged()
{
    QCustomPlot *plot = qobject_cast<QCustomPlot *>(sender());
    if (!plot)
        return;

    QList<QCPGraph *> selectedGraphs = plot->selectedGraphs();
    QList<QCPAbstractLegendItem *> selectedLegendItems = plot->legend->selectedItems();

    if (!selectedGraphs.isEmpty() && selectedLegendItems.isEmpty()) // 选中了曲线
    {
        for (QCPGraph *graph : selectedGraphs)
        {
            if (QCPPlottableLegendItem *item = plot->legend->itemWithPlottable(graph))
            {
                item->setSelected(true);
            }
        }
    }
    else if (!selectedLegendItems.isEmpty() && selectedGraphs.isEmpty()) // 选中了图例
    {
        for (QCPAbstractLegendItem *item : selectedLegendItems)
        {
            if (QCPPlottableLegendItem *pli = qobject_cast<QCPPlottableLegendItem *>(item))
            {
                if (QCPGraph *graph = qobject_cast<QCPGraph *>(pli->plottable()))
                {
                    QCPDataSelection selection = QCPDataSelection(graph->data()->dataRange());
                    graph->setSelection(selection);
                }
            }
        }
    }

    QList<QCPAbstractPlottable *> selected = plot->selectedPlottables();

    if (selected.isEmpty())
    {
        m_signalTree->setCurrentIndex(QModelIndex());
        return;
    }

    QCPGraph *graph = qobject_cast<QCPGraph *>(selected.first());
    if (!graph)
        return;

    QString uniqueID = graph->property("id").toString();
    if (uniqueID.isEmpty())
        return;

    QStandardItem *item = m_uniqueIdMap.value(uniqueID, nullptr);
    if (!item)
        return;

    {
        QSignalBlocker blocker(m_signalTree);
        m_signalTree->scrollTo(item->index(), QAbstractItemView::PositionAtCenter);
        m_signalTree->setCurrentIndex(item->index());
    }
}

// 清除所有子图信号的槽函数实现
void MainWindow::on_actionClearAllPlots_triggered()
{
    QMessageBox::StandardButton reply = QMessageBox::question(this, tr("Clear All Plots"),
                                                              tr("Are you sure you want to remove all signals from all plots?"),
                                                              QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    m_cursorManager->clearCursors();

    {
        const QSignalBlocker blocker(m_signalTreeModel);

        // 遍历当前记录在案的所有信号映射
        for (auto it = m_plotSignalMap.begin(); it != m_plotSignalMap.end(); ++it)
        {
            const QSet<QString> &signalIDs = it.value();
            for (const QString &uniqueID : signalIDs)
            {
                QStandardItem *item = m_uniqueIdMap.value(uniqueID, nullptr);
                if (item)
                {
                    item->setCheckState(Qt::Unchecked);
                }
            }
        }
    }

    // 3. 清空所有子图的 Graph 对象
    int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;

    for (QCustomPlot *plot : m_plotWidgets)
    {
        if (!plot)
            continue;

        plot->clearGraphs();
        configurePlotLegend(plot, legendMode);
        plot->replot();
    }

    m_plotSignalMap.clear();

    m_cursorManager->setupCursors();
    m_cursorManager->updateAllCursors();

    qDebug() << "All plots cleared.";
}

/**
 * @brief [辅助] 解析 sdi_view_meta_data.xml 的 QDomDocument
 */
MainWindow::LayoutInfo MainWindow::parseViewMetaData(const QDomDocument &doc)
{
    LayoutInfo info;
    QDomElement root = doc.documentElement(); // <sdi> 标签

    // 使用 firstChildElement 来安全地获取标签
    QDomElement rowsEl = root.firstChildElement("SubPlotRows");
    if (!rowsEl.isNull())
    {
        info.rows = rowsEl.text().toInt();
    }

    QDomElement colsEl = root.firstChildElement("SubPlotCols");
    if (!colsEl.isNull())
    {
        info.cols = colsEl.text().toInt();
    }

    QDomElement layoutEl = root.firstChildElement("LayoutType");
    if (!layoutEl.isNull())
    {
        info.layoutType = layoutEl.text();
    }

    return info;
}

/**
 * @brief [辅助] 解析 sdi_checked_signals.xml 的 QDomDocument
 */
QList<MainWindow::SignalInfo> MainWindow::parseCheckedSignals(const QDomDocument &doc)
{
    QList<SignalInfo> signalList;
    QDomElement root = doc.documentElement(); // <sdi> 标签

    // 1. 找到 <Signals> 标签
    QDomElement signalsNode = root.firstChildElement("Signals");
    if (signalsNode.isNull())
    {
        qWarning() << "Could not find <Signals> tag in sdi_checked_signals.xml";
        return signalList;
    }

    // 2. 遍历 <Signals> 下的所有子节点 (Sig1, Sig2, ...)
    QDomElement sigEl = signalsNode.firstChildElement(); // 从第一个 <Sig*> 开始
    while (!sigEl.isNull())
    {
        SignalInfo sigInfo;

        // 3. 提取普通文本标签
        sigInfo.name = sigEl.firstChildElement("SignalName").text();
        sigInfo.id = sigEl.firstChildElement("ID").text().toInt();

        // 4. 提取颜色
        QDomElement colorEl = sigEl.firstChildElement("Color");
        if (!colorEl.isNull())
        {
            qreal r = colorEl.firstChildElement("r").text().toDouble();
            qreal g = colorEl.firstChildElement("g").text().toDouble();
            qreal b = colorEl.firstChildElement("b").text().toDouble();
            // QColor::fromRgbF 用于 0.0-1.0 范围的浮点数
            sigInfo.color = QColor::fromRgbF(r, g, b);
        }

        // 5. 提取子图 ID (<Plots><Element>2</Element></Plots>)
        QDomElement plotsEl = sigEl.firstChildElement("Plots");
        if (!plotsEl.isNull())
        {
            // 找到所有名为 "Element" 的子标签
            QDomNodeList plotIdNodes = plotsEl.elementsByTagName("Element");
            for (int i = 0; i < plotIdNodes.count(); ++i)
            {
                sigInfo.plotIds.append(plotIdNodes.at(i).toElement().text().toInt());
            }
        }

        signalList.append(sigInfo);

        // 移动到下一个兄弟节点 (例如: 从 <Sig1> 到 <Sig2>)
        sigEl = sigEl.nextSiblingElement();
    }

    return signalList;
}

void MainWindow::showLoadProgress(int percentage)
{
    m_progressDialog->setValue(percentage);
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

        // 格式: "filename/tablename/"
        QString idPrefix = filename + "/" + table.name + "/";

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

            QString uniqueID = idPrefix + QString::number(i);

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

            // 将默认宽度为 2  QPen pen(color, 2); 宽度2绘制密集线段会卡
            QPen pen(color, 1);

            item->setData(QVariant::fromValue(pen), PenDataRole);

            parentItem->appendRow(item); // 添加到父条目 (文件或表)

            m_uniqueIdMap.insert(uniqueID, item);
        }
    }
}

void MainWindow::updateSignalTreeChecks()
{
    QSignalBlocker blocker(m_signalTreeModel);

    // 1. 获取当前活动子图的信号集合
    int activePlotIndex = m_plotWidgets.indexOf(m_activePlot);
    const QSet<QString> &activeSignals = m_plotSignalMap.value(activePlotIndex);

    // 2. 遍历所有已加载的信号条目
    for (auto it = m_uniqueIdMap.constBegin(); it != m_uniqueIdMap.constEnd(); ++it)
    {
        QStandardItem *item = it.value();
        QString uniqueID = it.key();

        // 3. 检查该信号是否在当前活动子图中
        bool shouldBeChecked = activeSignals.contains(uniqueID);
        Qt::CheckState newState = shouldBeChecked ? Qt::Checked : Qt::Unchecked;

        // 4. 仅在状态实际改变时才调用 setCheckState (虽然信号被阻塞，但这能减少 Model 内部的变更标记处理)
        if (item->checkState() != newState)
        {
            item->setCheckState(newState);
        }
    }
}

void MainWindow::onSignalItemChanged(QStandardItem *item)
{
    if (!item || !item->data(IsSignalItemRole).toBool())
        return;

    if (m_fileDataMap.isEmpty())
    {
        if (item->checkState() == Qt::Checked)
        {
            item->setCheckState(Qt::Unchecked);
        }
        return;
    }

    int plotIndex = m_plotWidgets.indexOf(m_activePlot);
    if (!m_activePlot || plotIndex == -1)
    {
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
            QMessageBox::information(this, tr("No Plot Selected"), tr("Please click on a plot to activate it before adding a signal."));
        }
        return;
    }

    QString uniqueID = item->data(UniqueIdRole).toString();
    if (uniqueID.isEmpty())
        return;

    if (item->checkState() == Qt::Checked)
    {
        addSignalToPlot(uniqueID, m_activePlot);
    }
    else
    {
        removeSignalFromPlot(uniqueID, m_activePlot);
    }
}

void MainWindow::onSignalItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;
    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);
    // 1. 检查是否为信号条目
    if (!item || !item->data(IsSignalItemRole).toBool())
        return;

    // 2. 检查点击位置
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

    // 3. 如果点击在预览线上，则打开新对话框
    QString uniqueID = item->data(UniqueIdRole).toString();
    QPen currentPen = item->data(PenDataRole).value<QPen>();

    // 使用新的自定义对话框
    SignalPropertiesDialog dialog(currentPen, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return; // 用户点击了 "Cancel"
    }

    QPen newPen = dialog.getSelectedPen(); // 获取包含所有属性的新 QPen

    item->setData(QVariant::fromValue(newPen), PenDataRole);

    // 更新所有图表中该信号的画笔
    for (QCustomPlot *plot : m_plotWidgets)
    {
        for (int i = 0; i < plot->graphCount(); ++i)
        {
            QCPGraph *graph = plot->graph(i);
            if (graph && graph->property("id").toString() == uniqueID)
            {
                graph->setPen(newPen);
                graph->parentPlot()->replot();
            }
        }
    }
}

// 信号树的右键菜单槽
void MainWindow::onSignalTreeContextMenu(const QPoint &pos)
{
    QModelIndex index = m_signalTree->indexAt(pos);
    if (!index.isValid())
        return;

    QStandardItem *item = m_signalTreeModel->itemFromIndex(index);

    if (!item || !item->data(IsFileItemRole).toBool())
        return; // 只在文件条目上显示菜单

    QString filename = item->data(FileNameRole).toString();

    QMenu contextMenu(this);
    QAction *deleteAction = contextMenu.addAction(tr("Remove '%1'").arg(filename));
    deleteAction->setData(filename); // 将文件名存储在动作中

    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteFileAction);
    contextMenu.exec(m_signalTree->viewport()->mapToGlobal(pos));
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
        m_cursorManager->setMode(CursorManager::SingleCursor);
    }
}

/**
 * @brief  获取全局时间范围
 */
QCPRange MainWindow::getGlobalTimeRange() const
{
    if (m_fileDataMap.isEmpty())
        return QCPRange(0, 1);

    bool first = true;
    QCPRange totalRange;
    for (const FileData &data : m_fileDataMap.values())
    {
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

    if (first)
        return QCPRange(0, 1);
    else
        return totalRange;
}

/**
 * @brief 估算数据时间步
 */
double MainWindow::getSmallestTimeStep() const
{
    // 查找所有文件中的最小步长
    double minStep = -1.0;

    for (const FileData &data : m_fileDataMap.values())
    {
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
 * @brief  从 m_plotGraphMap 中安全地获取一个 QCPGraph*
 * @param plot QCustomPlot 控件
 * @param uniqueID 信号的唯一ID ("filename/tablename/signalindex")
 * @return 如果找到则返回 QCPGraph*，否则返回 nullptr
 */
QCPGraph *MainWindow::getGraph(QCustomPlot *plot, const QString &uniqueID) const
{
    if (!plot)
        return nullptr;
    for (int i = 0; i < plot->graphCount(); ++i)
    {
        QCPGraph *graph = plot->graph(i);
        if (graph && graph->property("id").toString() == uniqueID)
        {
            return graph;
        }
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

    QStandardItem *itemToUncheck = m_uniqueIdMap.value(uniqueID, nullptr);

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

    const QSet<QString> signalIDsCopy = m_plotSignalMap.value(plotIndex);

    if (signalIDsCopy.isEmpty())
        return;

    qDebug() << "Clearing subplot index" << plotIndex << "- removing" << signalIDsCopy.size() << "signals.";

    for (const QString &uniqueID : signalIDsCopy)
    {
        QStandardItem *itemToUncheck = m_uniqueIdMap.value(uniqueID, nullptr);

        // 如果找到了，并且它当前被选中，则取消勾选它
        if (itemToUncheck && itemToUncheck->checkState() == Qt::Checked)
        {
            itemToUncheck->setCheckState(Qt::Unchecked);
        }
    }
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
    QModelIndex parentIndex = item->parent() ? item->parent()->index() : QModelIndex();
    m_signalTree->setRowHidden(item->row(), parentIndex, !visible);

    return visible;
}

/**
 * @brief [辅助] 在信号树中通过信号名称查找条目
 */
QStandardItem *MainWindow::findItemBySignalName(const QString &name)
{
    return findItemByName_Recursive(m_signalTreeModel->invisibleRootItem(), name);
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

/**
 * @brief [辅助] 应用导入的布局和信号设置
 * @param layout 从 sdi_view_meta_data.xml 解析的布局信息
 * @param signalList 从 sdi_checked_signals.xml 解析的信号列表
 */
void MainWindow::applyImportedView(const LayoutInfo &layout, const QList<SignalInfo> &signalList)
{
    {
        const QSignalBlocker blocker(m_signalTreeModel);

        QList<int> plotIndices = m_plotSignalMap.keys();
        for (int plotIndex : plotIndices)
        {
            const QSet<QString> signalIDsCopy = m_plotSignalMap.value(plotIndex);
            for (const QString &uniqueID : signalIDsCopy)
            {
                QStandardItem *item = m_uniqueIdMap.value(uniqueID, nullptr);
                if (item)
                {
                    item->setCheckState(Qt::Unchecked);
                }
            }
        }
    }

    m_plotSignalMap.clear();

    // 2. 设置新布局
    qDebug() << "Applying layout:" << layout.rows << "rows," << layout.cols << "cols";
    setupPlotLayout(layout.rows, layout.cols);

    // 确保我们有足够多的子图
    if (m_plotWidgets.isEmpty())
    {
        QMessageBox::warning(this, tr("Import Error"), tr("Failed to create plot layout."));
        return;
    }

    // 3. 遍历信号列表并应用设置 (包含索引转换)
    const int numRows = layout.rows;
    const int numCols = layout.cols;
    const int totalPlots = m_plotWidgets.size();

    if (numRows <= 0 || numCols <= 0 || totalPlots == 0)
    {
        QMessageBox::warning(this, tr("Import Error"), tr("Invalid layout dimensions."));
        return;
    }

    for (const SignalInfo &sig : signalList)
    {
        // 在树中查找信号
        QStandardItem *item = findItemBySignalName(sig.name);
        if (!item)
        {
            qWarning() << "Import View: Could not find signal in tree:" << sig.name;
            continue;
        }

        QString uniqueID = item->data(UniqueIdRole).toString();
        if (uniqueID.isEmpty())
            continue;

        // 更新颜色
        QPen currentPen = item->data(PenDataRole).value<QPen>();
        currentPen.setColor(sig.color);
        item->setData(QVariant::fromValue(currentPen), PenDataRole);

        // 遍历该信号应在的子图 ID
        for (int sdiPlotId : sig.plotIds)
        {
            if (sdiPlotId < 1)
                sdiPlotId = 1;

            // sdi 索引转换
            int r = (sdiPlotId - 1) % 8 + 1; // 1-based row
            int c = (sdiPlotId - 1) / 8 + 1; // 1-based col

            int plotIndex = (r - 1) * numCols + (c - 1);

            // 边界检查：如果计算出的行列超出了当前布局
            if (r > numRows || c > numCols)
            {
                if (plotIndex >= totalPlots)
                    plotIndex = 0;
            }

            if (plotIndex >= 0 && plotIndex < totalPlots)
            {
                QCustomPlot *targetPlot = m_plotWidgets.at(plotIndex);
                addSignalToPlot(uniqueID, targetPlot, false);

                {
                    const QSignalBlocker blocker(m_signalTreeModel);
                    item->setCheckState(Qt::Checked);
                }
            }
        }
    }

    int legendMode = m_legendPosGroup->checkedAction() ? m_legendPosGroup->checkedAction()->data().toInt() : 1;
    for (QCustomPlot *plot : m_plotWidgets)
    {
        plot->rescaleAxes();
        configurePlotLegend(plot, legendMode);
        plot->replot();
    }

    updateSignalTreeChecks();
    on_actionFitView_triggered();
    qDebug() << "Import View finished successfully.";
}

void MainWindow::on_actionFitViewYAll_triggered()
{
    performFitView(false, true, FitAllPlots);
}

/**
 * @brief [槽] 适应视图大小 (所有子图, X 和 Y 轴)
 */
void MainWindow::on_actionFitView_triggered()
{
    performFitView(true, true, FitAllPlots);
}

/**
 * @brief [槽] 适应视图大小 (所有子图, 仅 X 轴)
 */
void MainWindow::on_actionFitViewTime_triggered()
{
    performFitView(true, false, FitAllPlots);
}

/**
 * @brief [槽] 适应视图大小 (仅活动子图, 仅 Y 轴)
 */
void MainWindow::on_actionFitViewY_triggered()
{
    performFitView(false, true, FitActivePlot);
}

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

            plot->replot();
        }
    }

    // X轴变化时，游标也需要更新
    if (m_cursorManager->getMode() != CursorManager::NoCursor)
    {
        m_cursorManager->updateAllCursors();
    }
}

void MainWindow::on_actionLayoutCustom_triggered()
{
    // 1. 创建对话框
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
 * @brief [重写] 当文件被拖入窗口时调用
 */
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl &url : event->mimeData()->urls())
        {
            if (isSupportedFile(url.toLocalFile()))
            {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

/**
 * @brief [重写] 当文件在窗口上被放下时调用
 */
void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        for (const QUrl &url : mimeData->urls())
        {
            QString filePath = url.toLocalFile();
            if (filePath.endsWith(".mldatx", Qt::CaseInsensitive))
            {
                importView(filePath);
            }
            else if (isSupportedFile(filePath))
            {
                loadFile(filePath);
            }
        }
        event->acceptProposedAction();
    }
}

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
                        return true;
                    }
                }
            }
        }
        // 3. 处理放下事件 (Drop)
        else if (event->type() == QEvent::Drop)
        {
            QDropEvent *dropEvent = static_cast<QDropEvent *>(event);
            QByteArray encoded = dropEvent->mimeData()->data("application/x-qabstractitemmodeldatalist");
            QDataStream stream(&encoded, QIODevice::ReadOnly);

            int targetPlotIndex = m_plotWidgets.indexOf(targetPlot);
            if (targetPlotIndex == -1)
                return true;

            // 循环处理所有被拖拽的条目
            while (!stream.atEnd())
            {
                int r, c;
                QMap<int, QVariant> data;
                stream >> r >> c >> data; // 读取每个条目的数据

                if (data.contains(UniqueIdRole) && data.value(IsSignalItemRole).toBool())
                {
                    QString uniqueID = data.value(UniqueIdRole).toString();
                    QStandardItem *item = m_uniqueIdMap.value(uniqueID, nullptr);
                    if (!item)
                        continue;

                    bool alreadyOnPlot = m_plotSignalMap.value(targetPlotIndex).contains(uniqueID);

                    // 2. 如果不在，则添加它
                    if (!alreadyOnPlot)
                    {
                        setActivePlot(targetPlot);

                        if (item->checkState() == Qt::Unchecked)
                        {
                            item->setCheckState(Qt::Checked);
                        }
                        else
                        {
                            addSignalToPlot(uniqueID, targetPlot);
                        }
                    }
                }
            }
            dropEvent->acceptProposedAction();

            // 拖放完成后清除树的选择
            m_signalTree->clearSelection();

            updateSignalTreeChecks();
            return true;
        }
    }

    // 4. 将所有其他事件传递给基类
    return QMainWindow::eventFilter(watched, event);
}

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

    QCPAbstractPlottable *plottable = plot->plottableAt(pos, false);
    QCPGraph *graph = qobject_cast<QCPGraph *>(plottable);

    if (graph)
    {
        QString uniqueID = graph->property("id").toString();
        if (uniqueID.isEmpty())
            return;

        // 创建上下文菜单
        QMenu contextMenu(this);
        QAction *deleteAction = contextMenu.addAction(tr("Delete '%1'").arg(graph->name()));
        deleteAction->setData(uniqueID);
        connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSignalAction);

        // 在全局坐标位置显示菜单
        contextMenu.exec(plot->mapToGlobal(pos));
        return;
    }

    // 检查点击位置的顶层可布局元素
    QCPLayoutElement *el = plot->layoutElementAt(pos);

    // 尝试将元素转换为图例条目
    QCPAbstractLegendItem *legendItem = qobject_cast<QCPAbstractLegendItem *>(el);

    if (QCPPlottableLegendItem *plottableItem = qobject_cast<QCPPlottableLegendItem *>(legendItem))
    {
        // 2. 用户右键点击了 *图例条目*
        graph = qobject_cast<QCPGraph *>(plottableItem->plottable());
        if (!graph)
            return;

        QString uniqueID = graph->property("id").toString();
        if (uniqueID.isEmpty())
            return;

        // 创建上下文菜单
        QMenu contextMenu(this);
        QAction *deleteAction = contextMenu.addAction(tr("Delete '%1'").arg(graph->name()));
        deleteAction->setData(uniqueID);

        connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSignalAction);

        contextMenu.exec(plot->mapToGlobal(pos));
    }
    else if (qobject_cast<QCPAxisRect *>(el) || qobject_cast<QCPLegend *>(el))
    {
        // 找到此 plot 对应的 plotIndex
        int plotIndex = m_plotWidgets.indexOf(plot);
        if (plotIndex == -1)
            return;

        QMenu contextMenu(this);

        QAction *deleteSubplotAction = contextMenu.addAction(tr("Delete Subplot"));
        deleteSubplotAction->setData(plotIndex);
        connect(deleteSubplotAction, &QAction::triggered, this, &MainWindow::onDeleteSubplotAction);

        contextMenu.addSeparator();

        QAction *exportAction = contextMenu.addAction(tr("Export Image..."));
        connect(exportAction, &QAction::triggered, [this, plot]()
                { this->exportPlot(plot); });

        contextMenu.exec(plot->mapToGlobal(pos));
    }
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
 * @brief [辅助] 配置单个 Plot 的图例（类型、位置、样式）
 * @param plot 目标 Plot
 * @param mode 0: OutsideTop, 1: InsideTL, 2: InsideTR
 */
void MainWindow::configurePlotLegend(QCustomPlot *plot, int mode)
{
    if (!plot || !plot->axisRect())
        return;

    bool targetIsOutside = (mode == 0);

    //  检查当前图例类型是否已经匹配，避免不必要的 delete/new
    bool isFlow = (dynamic_cast<FlowLegend *>(plot->legend) != nullptr);
    bool needRecreate = (targetIsOutside != isFlow) || (plot->legend == nullptr);

    if (needRecreate)
    {
        if (plot->legend)
        {
            if (plot->legend->layout())
                plot->legend->layout()->take(plot->legend);
            delete plot->legend;
        }
        plot->legend = targetIsOutside ? new FlowLegend() : new QCPLegend();
    }

    // 2. 通用样式设置
    plot->legend->setVisible(m_toggleLegendAction->isChecked());
    QFont font = plot->font();
    font.setPointSize(7);
    plot->legend->setFont(font);
    plot->legend->setIconSize(10, 10);
    plot->legend->setIconTextPadding(3);
    plot->legend->setBorderPen(Qt::NoPen);
    plot->legend->setBrush(Qt::NoBrush);
    plot->legend->setMargins(QMargins(2, 2, 2, 2));

    // 允许选择图例项
    plot->legend->setSelectableParts(QCPLegend::spItems);
    plot->legend->setSelectedBrush(Qt::NoBrush);
    plot->legend->setSelectedBorderPen(Qt::NoPen);
    plot->legend->setSelectedTextColor(plot->legend->textColor());
    plot->legend->setSelectedFont(plot->legend->font());

    // 3. 布局逻辑
    QCPLayoutGrid *mainLayout = plot->plotLayout();
    if (!mainLayout)
        return;

    // 先将图例从现有布局中移除
    if (plot->legend->layout())
        plot->legend->layout()->take(plot->legend);

    if (targetIsOutside)
    {
        if (plot->graphCount() > 0)
        {
            if (mainLayout->rowCount() < 1 || (mainLayout->hasElement(0, 0) && mainLayout->element(0, 0) == plot->axisRect()))
            {
                mainLayout->insertRow(0);
            }

            mainLayout->addElement(0, 0, plot->legend);
            mainLayout->setRowSpacing(0);
            mainLayout->setRowStretchFactor(0, 0.001);
            // 强制宽度匹配
            plot->legend->setOuterRect(plot->axisRect()->outerRect());
        }
        else
        {
            mainLayout->simplify(); // 清理空行
        }
    }
    else
    {
        // 添加到内部
        QCPLayoutInset *insetLayout = plot->axisRect()->insetLayout();

        if (insetLayout->elementCount() == 0 || insetLayout->elementAt(0) != plot->legend)
        {
            Qt::Alignment align = Qt::AlignTop | (mode == 1 ? Qt::AlignLeft : Qt::AlignRight);
            insetLayout->addElement(plot->legend, align);
        }
    }

    // 重新添加图表项
    for (int i = 0; i < plot->graphCount(); ++i)
        plot->graph(i)->addToLegend(plot->legend);

    plot->replot();
}

/**
 * @brief  切换图例位置槽函数
 */
void MainWindow::onLegendPositionChanged(QAction *action)
{
    if (!action)
        return;
    int mode = action->data().toInt(); // 0: OutsideTop, 1: InsideTL, 2: InsideTR

    for (QCustomPlot *plot : m_plotWidgets)
    {
        configurePlotLegend(plot, mode);
    }
}

SignalLocation MainWindow::getSignalDataFromID(const QString &uniqueID) const
{
    SignalLocation loc;

    QStandardItem *item = m_uniqueIdMap.value(uniqueID, nullptr);
    if (!item)
        return loc;

    loc.name = item->text();
    loc.pen = item->data(PenDataRole).value<QPen>();

    // 2. 解析 ID
    QStringList parts = uniqueID.split('/');
    if (parts.size() < 2)
        return loc;

    QString filename = parts[0];
    if (!m_fileDataMap.contains(filename))
        return loc;
    const FileData &fileData = m_fileDataMap.value(filename);

    if (parts.size() == 2) // CSV: "filename/index"
    {
        if (!fileData.tables.isEmpty())
        {
            loc.table = &fileData.tables.first();
            loc.signalIndex = parts[1].toInt();
        }
    }
    else if (parts.size() == 3) // MAT: "filename/tablename/index"
    {
        QString tablename = parts[1];
        int idx = parts[2].toInt();
        for (const auto &table : fileData.tables)
        {
            if (table.name == tablename)
            {
                loc.table = &table;
                loc.signalIndex = idx;
                break;
            }
        }
    }
    return loc;
}

void MainWindow::onLayoutActionTriggered()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    QVariant data = action->data();

    if (data.type() == QVariant::Point)
    {
        QPoint grid = data.toPoint();
        setupPlotLayout(grid.x(), grid.y());
    }
    else if (data.type() == QVariant::String)
    {
        QString type = data.toString();
        QList<QRect> geometries;

        if (type == "split_bottom")
        {
            geometries << QRect(0, 0, 2, 1) << QRect(0, 1, 1, 1) << QRect(1, 1, 1, 1);
        }
        else if (type == "split_top")
        {
            geometries << QRect(0, 0, 1, 1) << QRect(1, 0, 1, 1) << QRect(0, 1, 2, 1);
        }
        else if (type == "split_left")
        {
            geometries << QRect(0, 0, 1, 2) << QRect(1, 0, 1, 1) << QRect(1, 1, 1, 1);
        }
        else if (type == "split_right")
        {
            geometries << QRect(0, 0, 1, 1) << QRect(0, 1, 1, 1) << QRect(1, 0, 1, 2);
        }

        if (!geometries.isEmpty())
        {
            setupPlotLayout(geometries);
        }
    }
}

void MainWindow::performFitView(bool fitX, bool fitY, FitTarget target)
{
    if (m_plotWidgets.isEmpty())
        return;

    QList<QCustomPlot *> targets;
    if (target == FitActivePlot)
    {
        if (m_activePlot)
            targets.append(m_activePlot);
    }
    else
    {
        targets = m_plotWidgets;
    }

    if (targets.isEmpty())
        return;

    QCPRange globalXRange;
    bool hasXRange = false;

    if (fitX)
    { // 扫描所有图表以获取全局时间
        for (QCustomPlot *plot : m_plotWidgets)
        {
            if (!plot)
                continue;
            for (int i = 0; i < plot->graphCount(); ++i)
            {
                QCPGraph *graph = plot->graph(i);
                if (graph && !graph->data()->isEmpty())
                {
                    bool found = false;
                    QCPRange r = graph->data()->keyRange(found);
                    if (found)
                    {
                        if (!hasXRange)
                        {
                            globalXRange = r;
                            hasXRange = true;
                        }
                        else
                        {
                            globalXRange.expand(r);
                        }
                    }
                }
            }
        }

        if (!hasXRange)
            globalXRange = QCPRange(0, 10);
    }

    // 应用 X 并计算/应用 Y
    for (QCustomPlot *plot : targets)
    {
        if (!plot)
            continue;

        if (fitX)
        {
            QSignalBlocker blocker(plot->xAxis);
            plot->xAxis->setRange(globalXRange);
        }

        if (fitY)
        {
            // 确定 Y 轴计算的参考 X 范围
            QCPRange searchXRange = fitX ? globalXRange : plot->xAxis->range();

            QCPRange foundYRange;
            bool hasY = false;

            for (int i = 0; i < plot->graphCount(); ++i)
            {
                QCPGraph *graph = plot->graph(i);
                if (!graph)
                    continue;

                bool found = false;
                // 获取在当前 X 视野内的 Y 范围
                QCPRange r = graph->getValueRange(found, QCP::sdBoth, searchXRange);
                if (found)
                {
                    if (!hasY)
                    {
                        foundYRange = r;
                        hasY = true;
                    }
                    else
                    {
                        foundYRange.expand(r);
                    }
                }
            }

            if (hasY)
            {
                // 添加 5% 边距
                double size = foundYRange.size();
                if (size == 0.0)
                    size = 1.0; // 防止单一直线的情况
                double margin = size * 0.05;
                // 特殊处理：如果值本身是0且范围也是0 (例如全0信号)
                if (margin == 0.05 && foundYRange.center() == 0.0)
                    margin = 0.5;

                foundYRange.lower -= margin;
                foundYRange.upper += margin;
                plot->yAxis->setRange(foundYRange);
            }
            else
            {
                plot->yAxis->setRange(0, 1); // 默认范围
            }
        }

        plot->replot();
    }

    // 如果缩放了 X 轴，手动触发一次游标更新 (因为我们屏蔽了信号)
    if (fitX)
    {
        if (!targets.isEmpty())
            onXAxisRangeChanged(targets.first()->xAxis->range());
    }
}

void MainWindow::setupGraphInstance(QCustomPlot *plot, const QString &uniqueID, const SignalLocation &loc)
{
    QCPGraph *graph = plot->addGraph();
    graph->setName(loc.name);
    graph->setData(loc.table->timeData, loc.table->valueData[loc.signalIndex]);
    graph->setPen(loc.pen);
    graph->setProperty("id", uniqueID);

    // 统一应用性能修复和样式设置
    if (graph->selectionDecorator())
    {
        QCPSelectionDecorator *decorator = graph->selectionDecorator();
        QPen selPen = decorator->pen();
        selPen.setWidth(loc.pen.width()); // 选中时保持宽度一致
        decorator->setPen(selPen);
        decorator->setBrush(Qt::NoBrush);
        decorator->setUsedScatterProperties(QCPScatterStyle::spNone); // 性能优化
    }
}

/**
 * @brief [辅助] 导出单个 Plot 为图片文件
 */
void MainWindow::exportPlot(QCustomPlot *plot)
{
    if (!plot)
        return;

    QString filters = tr("PNG Image (*.png);;JPG Image (*.jpg);;BMP Image (*.bmp);;PDF Document (*.pdf)");
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Plot"), "", filters);

    if (fileName.isEmpty())
        return;

    // --- 高清设置 ---
    double scale = 3.0; // 缩放因子：3.0 表示 3 倍分辨率 (约 300 DPI)
    int quality = 100;  // JPG 质量 (0-100)

    bool success = false;
    if (fileName.endsWith(".png", Qt::CaseInsensitive))
    {
        success = plot->savePng(fileName, 0, 0, scale, -1);
    }
    else if (fileName.endsWith(".jpg", Qt::CaseInsensitive) || fileName.endsWith(".jpeg", Qt::CaseInsensitive))
    {
        success = plot->saveJpg(fileName, 0, 0, scale, quality);
    }
    else if (fileName.endsWith(".bmp", Qt::CaseInsensitive))
    {
        success = plot->saveBmp(fileName, 0, 0, scale);
    }
    else if (fileName.endsWith(".pdf", Qt::CaseInsensitive))
    {
        success = plot->savePdf(fileName);
    }
    else
    {
        fileName += ".png";
        success = plot->savePng(fileName, 0, 0, scale, -1);
    }

    if (!success)
    {
        QMessageBox::warning(this, tr("Export Failed"), tr("Failed to save image to %1").arg(fileName));
    }
}

/**
 * @brief [槽] 导出整个布局视图
 */
void MainWindow::on_actionExportAll_triggered()
{
    if (!m_plotContainer)
        return;

    QString filters = tr("PNG Image (*.png);;JPG Image (*.jpg);;BMP Image (*.bmp)");
    QString fileName = QFileDialog::getSaveFileName(this, tr("Export All Views"), "", filters);

    if (fileName.isEmpty())
        return;

    if (!fileName.contains('.'))
    {
        fileName += ".png";
    }

    QPixmap pixmap = m_plotContainer->grab();
    bool success = pixmap.save(fileName, nullptr, 100);

    if (!success)
    {
        QMessageBox::warning(this, tr("Export Failed"), tr("Failed to save all views to %1").arg(fileName));
    }
}