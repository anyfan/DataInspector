#include "mainwindow.h"
#include "qcustomplot.h"
#include "signaltreedelegate.h" // <-- 新增：包含自定义委托

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
#include <QColorDialog> // <-- 新增：用于颜色选择
#include <QFrame>       // <-- 新增
#include <QVBoxLayout>  // <-- 新增

// 定义一个自定义角色 (Qt::UserRole + 1) 来存储 QPen
const int PenDataRole = Qt::UserRole + 1;

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

    // 注册 QPen 类型，以便在 QVariant 中使用
    qRegisterMetaType<QPen>("QPen");
}

MainWindow::~MainWindow()
{
    // ... (现有代码) ...
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
    // ... (现有代码) ...
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
            Qt::QueuedConnection); // <-- 修正：QueledConnection -> QueuedConnection

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
}

void MainWindow::createMenus()
{
    // ... (现有代码) ...
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

    // --- 新增：设置自定义委托 ---
    // 这将接管 QTreeView 中条目的绘制
    m_signalTree->setItemDelegate(new SignalTreeDelegate(m_signalTree));
    // ----------------------------

    m_signalDock->setWidget(m_signalTree);

    // ... (现有代码) ...
    m_signalDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    addDockWidget(Qt::LeftDockWidgetArea, m_signalDock);

    // 连接 itemChanged 信号 (替换双击) (5.1 节)
    connect(m_signalTreeModel, &QStandardItemModel::itemChanged, this, &MainWindow::onSignalItemChanged);
    // 新增：连接 doubleClicked 信号用于编辑 (需求 3)
    connect(m_signalTree, &QTreeView::doubleClicked, this, &MainWindow::onSignalItemDoubleClicked);
}

void MainWindow::setupPlotInteractions(QCustomPlot *plot)
{
    // ... (现有代码) ...
    plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectPlottables);
    plot->legend->setVisible(true);

    // 连接点击信号，用于设置 "active" plot (5.1 节)
    connect(plot, &QCustomPlot::mousePress, this, &MainWindow::onPlotClicked);
}

// --- 绘图布局 ---

void MainWindow::clearPlotLayout()
{
    // ... (现有代码) ...
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
    m_plotFrameMap.clear(); // <-- 新增：清理 frame 映射
    // 注意：m_plotGraphMap 在 onDataLoadFinished 中清理
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
            // --- 修复高亮 (需求 1) ---
            // 1. 创建一个用于高亮的 Frame
            QFrame *plotFrame = new QFrame(m_plotContainer);
            plotFrame->setFrameShape(QFrame::NoFrame);
            plotFrame->setLineWidth(2);
            // 默认边框 (透明)
            plotFrame->setStyleSheet("QFrame { border: 2px solid transparent; }");

            // 2. 将 plot 放入 Frame
            QVBoxLayout *frameLayout = new QVBoxLayout(plotFrame);
            frameLayout->setContentsMargins(0, 0, 0, 0);

            QCustomPlot *plot = new QCustomPlot(plotFrame); // <-- 父对象设为 frame
            setupPlotInteractions(plot);

            frameLayout->addWidget(plot); // <-- 将 plot 添加到 frame 的布局

            // 3. 将 Frame 添加到网格
            grid->addWidget(plotFrame, r, c); // <-- 添加 frame，而不是 plot
            m_plotWidgets.append(plot);

            // 4. 存储 plot 和 frame 的映射
            m_plotFrameMap.insert(plot, plotFrame);
            // -------------------------
        }
    }

    // 默认激活第一个 plot
    if (!m_plotWidgets.isEmpty())
    {
        m_activePlot = m_plotWidgets.first();

        // --- 修复高亮 (需求 1) ---
        // 立即应用视觉反馈 (到 Frame)
        QFrame *frame = m_plotFrameMap.value(m_activePlot);
        if (frame)
        {
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        }

        // --- 修复 Bug (需求 2) ---
        // 初始设置 plot 时，也需要同步一次勾选状态
        updateSignalTreeChecks();
        m_signalTree->viewport()->update(); // <-- 新增: 强制重绘
        // -------------------------
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
    // ... (现有代码) ...
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
    // 清理 graph 映射 (重构)
    m_plotGraphMap.clear();

    // 3. 填充信号树
    populateSignalTree(data);

    // --- 修复 Bug (需求 2) ---
    // 加载新数据后，需要立即同步勾选状态
    updateSignalTreeChecks();
    m_signalTree->viewport()->update(); // <-- 新增: 强制重绘
    // -------------------------

    QMessageBox::information(this, tr("Success"), tr("Successfully loaded %1 data points.").arg(data.timeData.count()));
}

void MainWindow::populateSignalTree(const CsvData &data)
{
    // 在清空模型时阻止信号，避免触发 onSignalItemChanged
    QSignalBlocker blocker(m_signalTreeModel);
    m_signalTreeModel->clear();
    m_signalPens.clear(); // <-- 新增：清空旧的样式

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
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);

        // 关键：将此信号在 m_loadedValueData 中的索引存储在 UserRole 中
        // (i-1) 是因为它在 valueData 向量中的索引
        item->setData(i - 1, Qt::UserRole);

        // --- 新增：预定义样式 (需求1) ---
        // 随机颜色
        QColor color(10 + (qrand() % 245), 10 + (qrand() % 245), 10 + (qrand() % 245));
        QPen pen(color, 1); // 1px 宽度

        m_signalPens.append(pen); // 存储
        // 将 QPen 存储在自定义角色中，供委托使用
        item->setData(QVariant::fromValue(pen), PenDataRole);
        // ---------------------------------

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
    // 1. 获取发送信号的 plot
    QCustomPlot *clickedPlot = qobject_cast<QCustomPlot *>(sender());
    if (!clickedPlot || clickedPlot == m_activePlot) // <-- 优化：如果点击的还是当前 plot，则不执行任何操作
        return;

    // 2. 将其设为 "active"
    m_activePlot = clickedPlot;

    // 3. (可选) 添加视觉反馈
    for (QCustomPlot *plot : m_plotWidgets)
    {
        // --- 修复高亮 (需求 1) ---
        QFrame *frame = m_plotFrameMap.value(plot);
        if (!frame)
            continue;

        if (plot == m_activePlot)
        {
            // 设置活动边框 (例如，蓝色)
            frame->setStyleSheet("QFrame { border: 2px solid #0078d4; }");
        }
        else
        {
            // 恢复默认边框
            frame->setStyleSheet("QFrame { border: 2px solid transparent; }");
        }
        // -------------------------
    }
    qDebug() << "Active plot set to:" << m_activePlot;

    // 4. --- [核心需求2] ---
    //    更新信号树的勾选状态以反映这个新激活的 plot
    updateSignalTreeChecks();
    m_signalTree->viewport()->update(); // <-- 新增: 强制重绘
}

/**
 * @brief [新增] 根据 m_activePlot 更新信号树的勾选状态
 */
void MainWindow::updateSignalTreeChecks()
{
    // 在我们批量修改勾选状态时，阻止 onSignalItemChanged 信号被触发
    QSignalBlocker blocker(m_signalTreeModel);

    // 获取当前活动 plot 上的图形 map
    // 如果 m_activePlot 在 m_plotGraphMap 中还没有条目 (例如，一个空 plot)，
    // .value() 会返回一个默认构造的 (空) QMap。
    const auto &activeGraphs = m_plotGraphMap.value(m_activePlot);

    for (int i = 0; i < m_signalTreeModel->rowCount(); ++i)
    {
        QStandardItem *item = m_signalTreeModel->item(i);
        if (!item)
            continue;

        int signalIndex = item->data(Qt::UserRole).toInt();

        // 检查这个信号是否存在于当前活动 plot 的 map 中
        if (activeGraphs.contains(signalIndex))
        {
            item->setCheckState(Qt::Checked);
        }
        else
        {
            item->setCheckState(Qt::Unchecked);
        }
    }
}

// <-- 槽函数 (替换 onSignalDoubleClicked) -->
void MainWindow::onSignalItemChanged(QStandardItem *item)
{
    if (!item)
        return;

    // 1. 检查数据和活动图表
    if (m_loadedTimeData.isEmpty())
    {
        if (item->checkState() == Qt::Checked)
        {
            // 使用 QSignalBlocker 防止在 setData 时递归调用
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
        }
        // 在数据加载前不显示消息，避免干扰
        return;
    }

    if (!m_activePlot)
    {
        if (item->checkState() == Qt::Checked)
        {
            QSignalBlocker blocker(m_signalTreeModel);
            item->setCheckState(Qt::Unchecked);
            QMessageBox::information(this, tr("No Plot Selected"), tr("Please click on a plot to activate it before adding a signal."));
        }
        return;
    }

    // 2. 获取信号信息
    int signalIndex = item->data(Qt::UserRole).toInt();
    QString signalName = item->text();

    if (signalIndex < 0 || signalIndex >= m_loadedValueData.count())
    {
        qWarning() << "Invalid signal index" << signalIndex;
        return;
    }

    // --- 信号槽阻断器 ---
    // 在我们修改 item (例如 setIcon) 时，不希望再次触发此槽
    // (虽然我们不再修改 item，但保留它作为好习惯)
    QSignalBlocker blocker(m_signalTreeModel);

    // 3. 根据勾选状态添加或移除图表 (只针对 m_activePlot)
    if (item->checkState() == Qt::Checked)
    {
        // --- 添加图表 ---

        // 检查是否已存在 (理论上 updateSignalTreeChecks 应该阻止这种情况，但作为安全检查)
        if (m_plotGraphMap.value(m_activePlot).contains(signalIndex))
        {
            qWarning() << "Graph already exists on this plot.";
            return;
        }

        qDebug() << "Adding signal" << signalName << "(index" << signalIndex << ") to plot" << m_activePlot;

        QCPGraph *graph = m_activePlot->addGraph();
        graph->setName(signalName);
        graph->setData(m_loadedTimeData, m_loadedValueData[signalIndex]);

        // --- 使用预定义的样式 (需求1) ---
        QPen pen = item->data(PenDataRole).value<QPen>();
        graph->setPen(pen);
        // ---------------------------------

        // 存储引用 (重构)
        m_plotGraphMap[m_activePlot].insert(signalIndex, graph);

        m_activePlot->rescaleAxes();
        m_activePlot->replot();
    }
    else // Qt::Unchecked
    {
        // --- 移除图表 ---

        // (重构)
        // 检查图表是否在 m_activePlot 的 map 中
        if (!m_plotGraphMap.value(m_activePlot).contains(signalIndex))
        {
            return; // 不在 Map 中，无需操作
        }

        QCPGraph *graph = m_plotGraphMap.value(m_activePlot).value(signalIndex);

        // ... (现有代码) ...
        if (graph) // graph 应该总是有效的
        {
            qDebug() << "Removing signal" << signalName << "from plot" << m_activePlot;

            m_activePlot->removeGraph(graph);                 // removeGraph 会自动 delete graph
            m_plotGraphMap[m_activePlot].remove(signalIndex); // 从 map 中移除

            // 只有在移除图表后才重绘
            m_activePlot->replot();
        }
    }
}

/**
 * @brief [新增] 响应双击信号，弹出颜色选择器 (需求 3)
 */
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

    // 1. 弹出颜色对话框
    QColor newColor = QColorDialog::getColor(currentPen.color(), this, tr("Select Signal Color"));

    if (!newColor.isValid())
    {
        return; // 用户取消
    }

    // 2. 创建新 Pen
    QPen newPen = currentPen;
    newPen.setColor(newColor);

    // 3. 更新模型 (这将触发 delegate 重绘预览)
    item->setData(QVariant::fromValue(newPen), PenDataRole);
    // 更新我们的样式缓存
    m_signalPens[signalIndex] = newPen;

    // 4. 更新所有图表上*已经存在*的这个信号
    // 遍历所有 plot (因为一个信号可能在多个 plot 上)
    for (auto it = m_plotGraphMap.begin(); it != m_plotGraphMap.end(); ++it)
    {
        // 检查该 plot 是否有这个信号
        if (it.value().contains(signalIndex))
        {
            QCPGraph *graph = it.value().value(signalIndex);
            if (graph)
            {
                graph->setPen(newPen);
                graph->parentPlot()->replot(); // 重绘
            }
        }
    }
}