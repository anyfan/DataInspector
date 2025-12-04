#include "mainwindow.h"
#include "plotmanager.h"
#include "replaymanager.h"
#include "cursormanager.h"
#include "scriptwindow.h"
#include "signalbrowser.h"
#include "scriptapi.h"
#include "types.h"
#include "viewloader.h"

#include <QMenuBar>
#include <QToolBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QProgressDialog>
#include <QDockWidget>
#include <QGridLayout>
#include <QInputDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDebug>
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"

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
      m_plotManager(nullptr),
      m_cursorManager(nullptr),
      m_replayManager(nullptr),
      m_signalBrowser(nullptr),
      m_plotContainer(nullptr),
      m_customLayoutDialog(nullptr),
      m_scriptAPI(nullptr),
      m_scriptWindow(nullptr),
      m_legendPosGroup(nullptr)
{
    setupDataManagerThread();

    // 1. 创建 Plot 容器
    m_plotContainer = new QWidget(this);
    setCentralWidget(m_plotContainer);

    // 2. 初始化 PlotManager
    m_plotManager = new PlotManager(m_plotContainer, this);

    // 3. 动作与菜单
    createActions();
    createDocks();
    createToolBars();
    createMenus();

    // 4. 初始化 CursorManager
    m_cursorManager = new CursorManager(&m_plotManager->getPlots(), this);
    m_currentCursorMode = CursorManager::SingleCursor;
    connect(m_cursorManager, &CursorManager::modeChanged, this, &MainWindow::onCursorModeChanged);

    // 5. 初始化 ReplayManager
    m_replayManager = new ReplayManager(m_replayAction, this);
    if (m_replayManager->getDockWidget())
        addDockWidget(Qt::BottomDockWidgetArea, m_replayManager->getDockWidget());

    connect(m_replayManager, &ReplayManager::replayTimeChanged,
            m_cursorManager, &CursorManager::updateCursors);

    connect(m_cursorManager, &CursorManager::cursorKeyChanged,
            m_replayManager, &ReplayManager::onCursorKeyChanged);

    // 6. 连接 PlotManager 信号
    connect(m_plotManager, &PlotManager::activePlotChanged, this, &MainWindow::onActivePlotChanged);
    connect(m_plotManager, &PlotManager::plotUpdated, this, &MainWindow::onPlotManagerUpdated);
    connect(m_plotManager, &PlotManager::viewChanged, m_cursorManager, &CursorManager::updateAllCursors);
    connect(m_plotManager, &PlotManager::layoutChanged, this, &MainWindow::onLayoutChanged);
    connect(m_plotManager, &PlotManager::signalDropRequested, this, &MainWindow::onSignalDropRequested);
    connect(m_plotManager, &PlotManager::signalSelectionChanged, this, &MainWindow::onSignalSelectionChanged);
    connect(m_plotManager, &PlotManager::removeSubplotRequested, this, &MainWindow::onRemoveSubplotRequested);
    connect(m_plotManager, &PlotManager::removeSignalRequested, this, &MainWindow::onRemoveSignalRequested);
    connect(m_plotManager, &PlotManager::plotUpdated, this, &MainWindow::plotUpdated);

    setWindowTitle(tr("Data Inspector (Refactored)"));
    resize(1280, 800);
    setAcceptDrops(true);

    // 7. 初始布局
    m_plotManager->setupLayout(2, 1);

    // 初始化图例在顶部
    m_plotManager->setLegendPosition(0);

    // 8. 进度条
    m_progressDialog = new QProgressDialog(this);
    m_progressDialog->reset();
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setAutoClose(true);
    m_progressDialog->hide();

    qRegisterMetaType<QPen>("QPen");
    m_scriptAPI = new ScriptAPI(this);
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

    m_exportViewJsonAction = new QAction(tr("Export View (JSON)..."), this);
    connect(m_exportViewJsonAction, &QAction::triggered, this, &MainWindow::on_actionExportViewJson_triggered);

    m_importViewJsonAction = new QAction(tr("Import View (JSON)..."), this);
    connect(m_importViewJsonAction, &QAction::triggered, this, &MainWindow::on_actionImportViewJson_triggered);

    // 替换布局菜单
    m_layout1x1Action = new QAction(tr("1x1 Layout"), this);
    m_layout1x1Action->setData(QPoint(1, 1));
    m_layout1x1Action->setIcon(QIcon(":/icon/IxEditorGridNone.svg"));

    m_layout1x2Action = new QAction(tr("1x2 Layout (Side by Side)"), this);
    m_layout1x2Action->setData(QPoint(1, 2));
    QPixmap layout1x2Pixmap(":/icon/F7RectangleGrid1x2.svg");
    QTransform layout1x2Transform;
    layout1x2Transform.rotate(90);
    m_layout1x2Action->setIcon(QIcon(QPixmap(layout1x2Pixmap.transformed(layout1x2Transform))));

    m_layout2x1Action = new QAction(tr("2x1 Layout (Stacked)"), this);
    m_layout2x1Action->setData(QPoint(2, 1));
    m_layout2x1Action->setIcon(QIcon(":/icon/F7RectangleGrid1x2.svg"));

    m_layout2x2Action = new QAction(tr("2x2 Layout"), this);
    m_layout2x2Action->setData(QPoint(2, 2));
    m_layout2x2Action->setIcon(QIcon(":/icon/grid.svg"));

    m_layoutSplitBottomAction = new QAction(tr("Bottom Split"), this);
    m_layoutSplitBottomAction->setData("split_bottom");
    QPixmap bottomSplitPixmap(":/icon/grid-split-right.svg");
    QTransform bottomSplitTransform;
    bottomSplitTransform.rotate(90);
    m_layoutSplitBottomAction->setIcon(QIcon(QPixmap(bottomSplitPixmap.transformed(bottomSplitTransform))));

    m_layoutSplitTopAction = new QAction(tr("Top Split"), this);
    m_layoutSplitTopAction->setData("split_top");
    QPixmap topSplitPixmap(":/icon/grid-split-right.svg");
    QTransform topSplitTransform;
    topSplitTransform.rotate(-90);
    m_layoutSplitTopAction->setIcon(QIcon(QPixmap(topSplitPixmap.transformed(topSplitTransform))));

    m_layoutSplitLeftAction = new QAction(tr("Left Split"), this);
    m_layoutSplitLeftAction->setData("split_left");
    QPixmap leftSplitPixmap(":/icon/grid-split-right.svg");
    QTransform leftSplitTransform;
    leftSplitTransform.rotate(180);
    m_layoutSplitLeftAction->setIcon(QIcon(QPixmap(leftSplitPixmap.transformed(leftSplitTransform))));

    m_layoutSplitRightAction = new QAction(tr("Right Split"), this);
    m_layoutSplitRightAction->setData("split_right");
    m_layoutSplitRightAction->setIcon(QIcon(":/icon/grid-split-right.svg"));

    QList<QAction *> layoutActions = {
        m_layout1x1Action, m_layout1x2Action, m_layout2x1Action, m_layout2x2Action,
        m_layoutSplitBottomAction, m_layoutSplitTopAction,
        m_layoutSplitLeftAction, m_layoutSplitRightAction};

    for (QAction *action : layoutActions)
    {
        connect(action, &QAction::triggered, this, &MainWindow::onLayoutActionTriggered);
    }

    m_layoutCustomAction = new QAction(tr("Custom Grid..."), this);
    m_layoutCustomAction->setIcon(QIcon(":/icon/MdiViewGridPlusOutline.svg"));
    connect(m_layoutCustomAction, &QAction::triggered, this, &MainWindow::on_actionLayoutCustom_triggered);

    // 视图缩放动作
    m_fitViewAction = new QAction(tr("Fit View"), this);
    m_fitViewAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    m_fitViewAction->setToolTip(tr("适应视图"));
    m_fitViewAction->setShortcut(Qt::Key_Space);
    connect(m_fitViewAction, &QAction::triggered, this, &MainWindow::on_actionFitView_triggered);

    m_fitViewTimeAction = new QAction(tr("Fit View (Time)"), this);
    m_fitViewTimeAction->setIcon(QIcon(":/icon/arrows_left_right.svg"));
    m_fitViewTimeAction->setToolTip(tr("适应视图（时间轴）"));
    m_fitViewTimeAction->setShortcut(QKeySequence(tr("Ctrl+Alt+T")));
    connect(m_fitViewTimeAction, &QAction::triggered, this, &MainWindow::on_actionFitViewTime_triggered);

    m_fitViewYAction = new QAction(tr("Fit View (Y-Axis)"), this);
    m_fitViewYAction->setIcon(QIcon(":/icon/arrows_up_down.svg"));
    m_fitViewYAction->setToolTip(tr("适应视图（Y轴）"));
    m_fitViewYAction->setShortcut(QKeySequence(tr("Ctrl+Alt+Y")));
    connect(m_fitViewYAction, &QAction::triggered, this, &MainWindow::on_actionFitViewY_triggered);

    m_fitViewYAllAction = new QAction(tr("Fit View All (Y-Axis)"), this);
    m_fitViewYAllAction->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_fitViewYAllAction->setToolTip(tr("适应所有子图视图（Y轴）"));
    m_fitViewYAllAction->setShortcut(QKeySequence(tr("Ctrl+Shift+Y")));
    connect(m_fitViewYAllAction, &QAction::triggered, this, &MainWindow::on_actionFitViewYAll_triggered);

    m_cursorActionSingle = new QAction(tr("单游标"), this);
    m_cursorActionSingle->setCheckable(false);
    m_cursorActionSingle->setIcon(QIcon(":/icon/cursor_1.svg"));
    m_cursorActionSingle->setData(QVariant::fromValue(CursorManager::SingleCursor));

    m_cursorActionDouble = new QAction(tr("双游标"), this);
    m_cursorActionDouble->setCheckable(false);
    m_cursorActionDouble->setIcon(QIcon(":/icon/cursor_2.svg"));
    m_cursorActionDouble->setData(QVariant::fromValue(CursorManager::DoubleCursor));

    m_cursorMenuGroup = new QActionGroup(this);
    m_cursorMenuGroup->addAction(m_cursorActionSingle);
    m_cursorMenuGroup->addAction(m_cursorActionDouble);

    // 创建工具按钮
    m_cursorMainBtn = new QToolButton(this);
    m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_1.svg"));
    m_cursorMainBtn->setCheckable(true);
    m_cursorMainBtn->setAutoRaise(true);
    m_cursorMainBtn->setToolTip(tr("启用/关闭游标"));

    // 设置样式表
    m_cursorMainBtn->setStyleSheet(
        "QToolButton {"
        "    border: none;"
        "    border-bottom: 3px solid transparent;"
        "    padding: 3px;"
        "    border-radius: 0px;"
        "}"
        "QToolButton:checked {"
        "    background-color: transparent;"
        "    border-bottom: 3px solid #0078d4;"
        "}"
        "QToolButton:hover {"
        "    background-color: rgba(0, 0, 0, 0.05);"
        "}");

    m_cursorArrowBtn = new QToolButton(this);
    m_cursorArrowBtn->setArrowType(Qt::DownArrow);
    m_cursorArrowBtn->setPopupMode(QToolButton::InstantPopup);
    m_cursorArrowBtn->setAutoRaise(true);
    m_cursorArrowBtn->setFixedWidth(12);
    m_cursorArrowBtn->setToolTip(tr("切换游标模式"));

    m_cursorArrowBtn->setStyleSheet(
        "QToolButton {"
        "    border: none;"
        "    padding: 0px;"
        "    border-radius: 0px;"
        "}"
        "QToolButton:hover {"
        "    background-color: rgba(0, 0, 0, 0.1);"
        "}"
        "QToolButton::menu-indicator { image: none; }");

    // 4. 创建菜单并赋值给按钮
    QMenu *cursorMenu = new QMenu(this);
    cursorMenu->addAction(m_cursorActionSingle);
    cursorMenu->addAction(m_cursorActionDouble);
    m_cursorArrowBtn->setMenu(cursorMenu);

    connect(m_cursorMainBtn, &QToolButton::toggled, this, &MainWindow::onCursorMainButtonToggled);
    connect(m_cursorMenuGroup, &QActionGroup::triggered, this, &MainWindow::onCursorMenuActionTriggered);

    m_replayAction = new QAction(tr("重放"), this);
    m_replayAction->setCheckable(true);
    m_replayAction->setIcon(QIcon(":/icon/play-circle.svg"));
    connect(m_replayAction, &QAction::toggled, this, &MainWindow::onReplayActionToggled);

    // 恢复图例相关的 Action 和 Group
    m_legendPosGroup = new QActionGroup(this);

    m_legendPosNoneAction = new QAction(tr("无"), this);
    m_legendPosNoneAction->setCheckable(true);
    m_legendPosNoneAction->setData(-1);

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

    m_legendPosGroup->addAction(m_legendPosNoneAction);
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

    m_antialiasingAction = new QAction(tr("启用曲线抗锯齿"), this);
    m_antialiasingAction->setToolTip(tr("开启/关闭曲线的抗锯齿渲染。\n关闭抗锯齿可消除陡峭线条的'幽灵线'假象并提高性能，但线条边缘会有锯齿。"));
    m_antialiasingAction->setCheckable(true);
    m_antialiasingAction->setChecked(true); // 默认开启
    connect(m_antialiasingAction, &QAction::toggled, this, &MainWindow::onAntialiasingActionToggled);

    // 设置默认线宽动作
    m_setDefaultPenWidthAction = new QAction(tr("默认线宽"), this);
    m_setDefaultPenWidthAction->setToolTip(tr("设置新加载信号的默认线宽"));
    connect(m_setDefaultPenWidthAction, &QAction::triggered, this, &MainWindow::on_actionSetDefaultPenWidth_triggered);

    m_clearAllPlotsAction = new QAction(tr("Clear All Plots"), this);
    m_clearAllPlotsAction->setToolTip(tr("Remove all signals from all plots"));
    m_clearAllPlotsAction->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
    m_clearAllPlotsAction->setShortcut(QKeySequence(tr("Ctrl+D")));
    connect(m_clearAllPlotsAction, &QAction::triggered, this, &MainWindow::on_actionClearAllPlots_triggered);

    m_maximizeAction = new QAction(this);
    m_maximizeAction->setIcon(QIcon(":/icon/arrows-angle-expand.svg"));
    m_maximizeAction->setToolTip(tr("Maximize Active Plot"));
    m_maximizeAction->setShortcut(Qt::Key_M);
    connect(m_maximizeAction, &QAction::triggered, this, &MainWindow::on_actionMaximize_triggered);

    m_fullScreenAction = new QAction(this);
    m_fullScreenAction->setIcon(QIcon(":/icon/fullscreen.svg"));
    m_fullScreenAction->setToolTip(tr("Full Screen"));
    m_fullScreenAction->setShortcut(Qt::Key_F11);
    connect(m_fullScreenAction, &QAction::triggered, this, &MainWindow::on_actionFullScreen_triggered);

    m_scriptConsoleAction = new QAction(tr("Script Console"), this);
    m_scriptConsoleAction->setShortcut(QKeySequence(Qt::Key_F12)); // 快捷键 F12
    connect(m_scriptConsoleAction, &QAction::triggered, this, &MainWindow::on_actionScriptConsole_triggered);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&文件"));
    fileMenu->addAction(m_loadFileAction);
    fileMenu->addAction(m_importViewAction);
    fileMenu->addAction(m_importViewJsonAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAllAction);
    fileMenu->addAction(m_exportViewJsonAction);

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

    // 创建 "设置" 菜单
    QMenu *settingsMenu = menuBar()->addMenu(tr("&设置"));
    settingsMenu->addAction(m_openGLAction);
    settingsMenu->addAction(m_setDefaultPenWidthAction);
    settingsMenu->addAction(m_antialiasingAction);

    QMenu *legendPosMenu = settingsMenu->addMenu(tr("图例位置"));
    // 恢复菜单项添加
    legendPosMenu->addAction(m_legendPosNoneAction);
    legendPosMenu->addSeparator();
    legendPosMenu->addAction(m_legendPosOutsideTopAction);
    legendPosMenu->addAction(m_legendPosInsideTLAction);
    legendPosMenu->addAction(m_legendPosInsideTRAction);

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_scriptConsoleAction);
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

    // 添加清除按钮到工具栏
    m_viewToolBar->addAction(m_clearAllPlotsAction);
    m_viewToolBar->addSeparator();

    // 添加游标动作
    QWidget *cursorContainer = new QWidget(m_viewToolBar);
    QHBoxLayout *hLayout = new QHBoxLayout(cursorContainer);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(0);
    hLayout->addWidget(m_cursorMainBtn);
    hLayout->addWidget(m_cursorArrowBtn);
    m_viewToolBar->addWidget(cursorContainer);

    m_viewToolBar->addSeparator();
    m_viewToolBar->addAction(m_maximizeAction);
    m_viewToolBar->addAction(m_fullScreenAction);

    m_viewToolBar->addSeparator();
    m_viewToolBar->addAction(m_replayAction);

    addToolBar(Qt::TopToolBarArea, m_viewToolBar);
}

void MainWindow::createDocks()
{
    m_signalDock = new QDockWidget(tr("信号"), this);
    m_signalBrowser = new SignalBrowser(m_signalDock);
    connect(m_signalBrowser, &SignalBrowser::signalCheckStateChanged, this, &MainWindow::onSignalCheckStateChanged);
    connect(m_signalBrowser, &SignalBrowser::signalPenChanged, this, &MainWindow::onSignalPenChanged);
    connect(m_signalBrowser, &SignalBrowser::fileRemoveRequested, this, &MainWindow::onFileRemoveRequested);
    m_signalDock->setWidget(m_signalBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);
}

void MainWindow::onSignalCheckStateChanged(const QString &uniqueId, bool checked)
{
    if (m_fileDataMap.isEmpty())
        return;

    if (checked)
    {
        SignalLocation loc = getSignalDataFromID(uniqueId);
        m_plotManager->addSignal(uniqueId, loc);
    }
    else
    {
        m_plotManager->removeSignal(uniqueId);
    }
}

void MainWindow::onSignalPenChanged(const QString &uniqueId, const QPen &newPen)
{
    // 遍历所有子图，查找包含该 uniqueID 的 graph 并更新 pen
    for (QCustomPlot *plot : m_plotManager->getPlots())
    {
        for (int i = 0; i < plot->graphCount(); ++i)
        {
            QCPGraph *graph = plot->graph(i);
            if (graph && graph->property("id").toString() == uniqueId)
            {
                graph->setPen(newPen);

                if (graph->selectionDecorator())
                {
                    QPen selPen = graph->selectionDecorator()->pen();
                    selPen.setWidth(newPen.width());
                    graph->selectionDecorator()->setPen(selPen);
                }
                plot->replot();
            }
        }
    }
}

void MainWindow::onFileRemoveRequested(const QString &filename)
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("Remove File"),
                                  tr("Are you sure you want to remove all data and graphs from file '%1'?").arg(filename),
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes)
    {
        removeFile(filename);
    }
}

void MainWindow::loadFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    m_progressDialog->setValue(0);
    m_progressDialog->setLabelText(tr("Loading %1...").arg(QFileInfo(filePath).fileName()));
    m_progressDialog->show();

    if (filePath.endsWith(".mat", Qt::CaseInsensitive))
        emit requestLoadMat(filePath);
    else
        emit requestLoadCsv(filePath);
}

void MainWindow::on_actionLoadFile_triggered()
{
    loadFile(QFileDialog::getOpenFileName(this, tr("Open File"), "", tr("Data Files (*.csv *.txt *.mat)")));
}

void MainWindow::importView(const QString &filePath)
{
    ViewData viewData;
    bool success = ViewLoader::loadFromZip(filePath, viewData);

    if (!success)
    {
        QMessageBox::critical(this, tr("Import Error"), tr("Failed to load view file."));
        return;
    }
    applyViewData(viewData);
}

void MainWindow::on_actionImportView_triggered()
{
    QString mldatxFilePath = QFileDialog::getOpenFileName(this,
                                                          tr("Import Simulink View"), "", tr("Simulink Data (*.mldatx)"));

    if (mldatxFilePath.isEmpty())
        return;

    importView(mldatxFilePath);
}

void MainWindow::onDataLoadFinished(const FileData &data)
{
    m_progressDialog->hide();
    QString filename = QFileInfo(data.filePath).fileName();
    if (m_fileDataMap.contains(filename))
    {
        if (QMessageBox::question(this, tr("File exists"), tr("Overwrite?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
            return;
        removeFile(filename);
    }
    m_fileDataMap.insert(filename, data);
    m_signalBrowser->loadFileData(data);
    updateReplayManagerRange();
    emit dataProcessingFinished(data.filePath);
}

void MainWindow::onDataLoadFailed(const QString &filePath, const QString &err)
{
    m_progressDialog->hide();
    QMessageBox::warning(this, tr("Error"), err);
}

void MainWindow::removeFile(const QString &filename)
{
    if (!m_fileDataMap.remove(filename))
        return;
    m_cursorManager->clearCursors();

    // 移除所有相关曲线
    m_plotManager->removeFileSignals(filename);
    m_signalBrowser->removeFile(filename);

    updateReplayManagerRange();
}

void MainWindow::showLoadProgress(int percentage)
{
    m_progressDialog->setValue(percentage);
}
void MainWindow::onReplayActionToggled(bool checked)
{
    if (checked && m_cursorManager->getMode() == CursorManager::NoCursor)
    {
        m_cursorActionSingle->setChecked(true);
        m_currentCursorMode = CursorManager::SingleCursor;

        m_cursorMainBtn->setChecked(true);
    }
}

QCPRange MainWindow::getGlobalTimeRange() const
{
    if (m_fileDataMap.isEmpty())
        return QCPRange(0, 10);

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
        return QCPRange(0, 10);
    return totalRange;
}

double MainWindow::getSmallestTimeStep() const
{
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
    return (minStep > 0) ? minStep : 0.01;
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
        m_plotManager->setupLayout(rows, cols);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls())
    {
        QString path = url.toLocalFile();
        if (path.endsWith(".mldatx", Qt::CaseInsensitive))
            importView(path);
        else if (isSupportedFile(path))
            loadFile(path);
    }
    event->acceptProposedAction();
}

void MainWindow::updateReplayManagerRange()
{
    if (m_replayManager)
        m_replayManager->updateDataRange(getGlobalTimeRange(), getSmallestTimeStep());
}

SignalLocation MainWindow::getSignalDataFromID(const QString &uniqueID) const
{
    SignalLocation loc;
    loc.pen = m_signalBrowser->getSignalPen(uniqueID);
    loc.name = m_signalBrowser->getSignalName(uniqueID);

    QStringList parts = uniqueID.split('/');
    if (parts.size() < 2)
        return loc;

    QString filename = parts[0];
    if (!m_fileDataMap.contains(filename))
        return loc;
    const FileData &fileData = m_fileDataMap.value(filename);

    if (parts.size() == 2 && !fileData.tables.isEmpty())
    { // CSV
        loc.table = &fileData.tables.first();
        loc.signalIndex = parts[1].toInt();
    }
    else if (parts.size() == 3)
    { // MAT
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
    if (m_plotManager->isMaximized())
        m_plotManager->toggleMaximizeActive();

    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    QVariant data = action->data();

    if (data.type() == QVariant::Point)
    {
        QPoint grid = data.toPoint();
        m_plotManager->setupLayout(grid.x(), grid.y());
    }
    else if (data.type() == QVariant::String)
    {
        QString type = data.toString();
        QList<QRect> geometries;
        if (type == "split_bottom")
            geometries << QRect(0, 0, 2, 1) << QRect(0, 1, 1, 1) << QRect(1, 1, 1, 1);
        else if (type == "split_top")
            geometries << QRect(0, 0, 1, 1) << QRect(1, 0, 1, 1) << QRect(0, 1, 2, 1);
        else if (type == "split_left")
            geometries << QRect(0, 0, 1, 1) << QRect(0, 1, 1, 1) << QRect(1, 0, 1, 2);
        else if (type == "split_right")
            geometries << QRect(0, 0, 1, 2) << QRect(1, 0, 1, 1) << QRect(1, 1, 1, 1);

        if (!geometries.isEmpty())
            m_plotManager->setupLayout(geometries);
    }
}

void MainWindow::on_actionMaximize_triggered()
{
    m_plotManager->toggleMaximizeActive();
    if (m_plotManager->isMaximized())
    {
        m_maximizeAction->setIcon(QIcon(":/icon/arrows-angle-contract.svg"));
        m_maximizeAction->setToolTip(tr("Restore Layout"));
    }
    else
    {
        m_maximizeAction->setIcon(QIcon(":/icon/arrows-angle-expand.svg"));
        m_maximizeAction->setToolTip(tr("Maximize Active Plot"));
    }
}
void MainWindow::onCursorMainButtonToggled(bool checked)
{
    if (checked)
    {
        m_cursorManager->setMode((CursorManager::CursorMode)m_currentCursorMode);

        if (m_currentCursorMode == CursorManager::SingleCursor)
        {
            m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_1.svg"));
        }
        else
        {
            m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_2.svg"));
        }
    }
    else
    {
        m_cursorManager->setMode(CursorManager::NoCursor);
    }
}

void MainWindow::onCursorMenuActionTriggered(QAction *action)
{
    if (!action)
        return;

    CursorManager::CursorMode newMode = action->data().value<CursorManager::CursorMode>();
    m_currentCursorMode = newMode;

    if (newMode == CursorManager::SingleCursor)
    {
        m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_1.svg"));
    }
    else
    {
        m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_2.svg"));
    }

    if (!m_cursorMainBtn->isChecked())
    {
        m_cursorMainBtn->setChecked(true);
    }
    else
    {
        m_cursorManager->setMode((CursorManager::CursorMode)m_currentCursorMode);
    }
}

void MainWindow::on_actionFullScreen_triggered()
{
    if (isFullScreen())
    {
        showNormal();
        m_fullScreenAction->setIcon(QIcon(":/icon/fullscreen.svg"));
        m_fullScreenAction->setToolTip(tr("Full Screen"));
    }
    else
    {
        showFullScreen();
        m_fullScreenAction->setIcon(QIcon(":/icon/fullscreen-exit.svg"));
        m_fullScreenAction->setToolTip(tr("Exit Full Screen"));
    }
}

void MainWindow::on_actionSetDefaultPenWidth_triggered()
{
    bool ok;
    int currentWidth = m_signalBrowser->defaultPenWidth();

    QString labelText = tr("线宽 (px):\n\n"
                           "注意：\n"
                           "1. 线宽大于 1 可能会增加 CPU 消耗 (渲染开销变大)。\n"
                           "2. 此设置将在下次加载文件时生效。");

    int width = QInputDialog::getInt(this, tr("设置默认线宽"),
                                     labelText, currentWidth, 1, 20, 1, &ok);
    if (ok)
    {
        m_signalBrowser->setDefaultPenWidth(width);
    }
}

void MainWindow::on_actionScriptConsole_triggered()
{
    if (!m_scriptWindow)
    {
        m_scriptWindow = new ScriptWindow(m_scriptAPI, this);
        m_scriptAPI->setScriptWindow(m_scriptWindow);
    }

    m_scriptWindow->show();
    m_scriptWindow->raise();
    m_scriptWindow->activateWindow();
}

void MainWindow::onSignalDropRequested(const QString &uniqueId, QCustomPlot *plot)
{
    SignalLocation loc = getSignalDataFromID(uniqueId);
    m_plotManager->addSignal(uniqueId, loc, plot);
    m_signalBrowser->setSignalChecked(uniqueId, true, true);
}

void MainWindow::onActivePlotChanged(QCustomPlot *plot)
{
    QSet<QString> ids = m_plotManager->getActivePlotSignalIDs();
    m_signalBrowser->updateChecksForActivePlot(ids);

    m_cursorManager->setActivePlot(plot);
}

void MainWindow::onPlotManagerUpdated()
{
    m_cursorManager->setupCursors();

    QTimer::singleShot(0, m_cursorManager, &CursorManager::updateAllCursors);
}

void MainWindow::onSignalSelectionChanged(const QString &id)
{
    m_signalBrowser->selectSignal(id);
}

void MainWindow::onRemoveSubplotRequested(int index)
{
    QSet<QString> ids = m_plotManager->getPlotSignalIDs(index);
    for (const QString &id : ids)
        m_signalBrowser->setSignalChecked(id, false, false);
}

void MainWindow::onRemoveSignalRequested(const QString &id)
{
    m_signalBrowser->setSignalChecked(id, false, false);
}

void MainWindow::on_actionFitView_triggered() { m_plotManager->performFitView(true, true, PlotManager::FitAllPlots); }
void MainWindow::on_actionFitViewTime_triggered() { m_plotManager->performFitView(true, false, PlotManager::FitAllPlots); }
void MainWindow::on_actionFitViewY_triggered() { m_plotManager->performFitView(false, true, PlotManager::FitActivePlot); }
void MainWindow::on_actionFitViewYAll_triggered() { m_plotManager->performFitView(false, true, PlotManager::FitAllPlots); }
void MainWindow::onOpenGLActionToggled(bool c) { m_plotManager->setOpenGL(c); }
void MainWindow::onAntialiasingActionToggled(bool c) { m_plotManager->setAntialiasing(c); }
void MainWindow::onLegendPositionChanged(QAction *a) { m_plotManager->setLegendPosition(a->data().toInt()); }
void MainWindow::on_actionClearAllPlots_triggered()
{
    if (QMessageBox::question(this, "Clear", "Clear all?") == QMessageBox::Yes)
    {
        if (m_cursorManager)
        {
            m_cursorManager->clearCursors();
        }

        m_plotManager->clearAllPlots();

        if (m_signalBrowser)
        {
            m_signalBrowser->uncheckAll();
        }

        updateReplayManagerRange();
    }
}

void MainWindow::on_actionExportAll_triggered() { m_plotManager->exportAllViews(QFileDialog::getSaveFileName(this, tr("Save Image"))); }

void MainWindow::onLayoutChanged()
{
    // 重新添加所有信号
    for (QCustomPlot *plot : m_plotManager->getPlots())
    {
        int idx = m_plotManager->getPlots().indexOf(plot);
        QSet<QString> ids = m_plotManager->getPlotSignalIDs(idx);

        for (const QString &id : ids)
        {
            if (!m_plotManager->getGraph(plot, id))
            {
                SignalLocation loc = getSignalDataFromID(id);
                if (loc.table)
                {
                    m_plotManager->addSignal(id, loc, plot, false, false, false);
                }
            }
        }
    }

    // 1. 统一构建一次图例 (极大地减少开销)
    m_plotManager->updateLegends();

    // 2. 进行一次 Y 轴全适应
    m_plotManager->performFitView(false, true, PlotManager::FitAllPlots);

    m_cursorManager->reset();
}

ViewData MainWindow::captureCurrentViewData() const
{
    ViewData data;

    QGridLayout *grid = qobject_cast<QGridLayout *>(m_plotManager->getContainer()->layout());
    if (grid)
    {
        data.layout.rows = grid->rowCount();
        data.layout.cols = grid->columnCount();
        // data.layout.layoutType = ... // 可选记录
    }
    else
    {
        data.layout.rows = 1;
        data.layout.cols = 1;
    }

    QMap<QString, ViewSignalInfo> signalMap;

    const QList<QCustomPlot *> &plots = m_plotManager->getPlots();

    for (int i = 0; i < plots.size(); ++i)
    {
        QCustomPlot *plot = plots[i];
        QSet<QString> ids = m_plotManager->getPlotSignalIDs(i);

        for (const QString &id : ids)
        {
            if (!signalMap.contains(id))
            {
                ViewSignalInfo info;
                info.uniqueId = id;
                info.name = m_signalBrowser->getSignalName(id);         // 获取名称
                info.color = m_signalBrowser->getSignalPen(id).color(); // 获取颜色
                signalMap.insert(id, info);
            }

            signalMap[id].plotIds.append(i);
        }
    }

    data.signalList = signalMap.values();
    return data;
}

void MainWindow::on_actionExportViewJson_triggered()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Export View to JSON"), "", tr("JSON Files (*.json)"));
    if (path.isEmpty())
        return;

    ViewData data = captureCurrentViewData();
    if (ViewLoader::saveToJson(path, data))
    {
        QMessageBox::information(this, tr("Success"), tr("View exported successfully."));
    }
    else
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to export view."));
    }
}

void MainWindow::on_actionImportViewJson_triggered()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Import View from JSON"), "", tr("JSON Files (*.json)"));
    if (path.isEmpty())
        return;

    ViewData viewData;
    if (ViewLoader::loadFromJson(path, viewData))
    {
        applyViewData(viewData);
    }
    else
    {
        QMessageBox::warning(this, tr("Error"), tr("Failed to load JSON view file."));
    }
}

void MainWindow::applyViewData(const ViewData &viewData)
{
    qDebug().noquote() << QString("Applying Layout: %1x%2").arg(viewData.layout.rows).arg(viewData.layout.cols);

    // 清除 PlotManager 中的所有信号记录
    int currentPlotCount = m_plotManager->getPlots().size();
    for (int i = 0; i < currentPlotCount; ++i)
    {
        QSet<QString> activeIds = m_plotManager->getPlotSignalIDs(i);
        for (const QString &id : activeIds)
        {
            m_signalBrowser->setSignalChecked(id, false, true);
        }
    }

    m_plotManager->clearAllPlots();
    m_plotManager->setupLayout(viewData.layout.rows, viewData.layout.cols);

    int totalPlots = m_plotManager->getPlots().size();
    if (totalPlots == 0)
        return;

    m_signalBrowser->setUpdatesEnabled(false);

    for (const ViewSignalInfo &sig : viewData.signalList)
    {
        QString uniqueID = sig.uniqueId;
        bool needLookup = uniqueID.isEmpty() || (getSignalDataFromID(uniqueID).table == nullptr);

        if (needLookup)
        {
            if (QStandardItem *item = m_signalBrowser->findItemByName(sig.name))
            {
                uniqueID = item->data(TreeItemRoles::UniqueIdRole).toString();
            }
        }

        if (!uniqueID.isEmpty())
        {
            SignalLocation loc = getSignalDataFromID(uniqueID);
            // 可选：如果 JSON 里有颜色，应用它
            // if (sig.color.isValid()) loc.pen.setColor(sig.color);

            for (int sdiPlotId : sig.plotIds)
            {
                if (sdiPlotId >= 0 && sdiPlotId < totalPlots)
                {
                    QCustomPlot *targetPlot = m_plotManager->getPlots().at(sdiPlotId);
                    m_plotManager->addSignal(uniqueID, loc, targetPlot, false, false, false);
                    m_signalBrowser->setSignalChecked(uniqueID, true, true);
                }
            }
        }
        else
        {
            qWarning() << "View Import: Signal not found:" << sig.name;
        }
    }

    m_signalBrowser->setUpdatesEnabled(true);
    m_plotManager->updateLegends();
    emit viewImportFinished();
}

bool MainWindow::exportViewToJson(const QString &path)
{
    ViewData data = captureCurrentViewData();
    return ViewLoader::saveToJson(path, data);
}

bool MainWindow::importViewFromJson(const QString &path)
{
    ViewData viewData;
    if (ViewLoader::loadFromJson(path, viewData))
    {
        applyViewData(viewData);
        return true;
    }
    return false;
}

void MainWindow::onCursorModeChanged(CursorManager::CursorMode mode)
{
    const QSignalBlocker blocker(m_cursorMainBtn);

    if (mode == CursorManager::NoCursor)
    {
        // 关闭状态：按钮弹起
        m_cursorMainBtn->setChecked(false);
    }
    else
    {
        // 开启状态：按钮按下
        m_cursorMainBtn->setChecked(true);

        // 更新内部记录的当前模式，以便下次点击切换时能恢复正确的模式
        m_currentCursorMode = mode;

        // 根据模式更新按钮图标
        if (mode == CursorManager::SingleCursor)
        {
            m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_1.svg"));
        }
        else if (mode == CursorManager::DoubleCursor)
        {
            m_cursorMainBtn->setIcon(QIcon(":/icon/cursor_2.svg"));
        }
    }
}