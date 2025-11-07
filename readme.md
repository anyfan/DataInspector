构建对标Simulink Data Inspector的高性能C++/Qt时间序列可视化平台：架构与实现方案第一部分：高性能数据检查器的架构蓝图1.1 引言：定义核心挑战与目标本方案旨在为构建一个功能对标 MathWorks Simulink Data Inspector (SDI) 1 的高性能C++/Qt桌面应用程序提供一个全面、深入且可执行的技术蓝图。SDI的核心功能是加载、浏览、绘制和分析来自多次仿真运行 (Runs) 的海量时间序列信号数据。用户的核心需求是处理“可能比较大”的数据量。在高性能可视化领域，这并非一个单一问题，而是三个独立的、必须分别解决的工程挑战 3：I/O 瓶颈： 应用程序必须能够从磁盘高效读取数 GB 甚至数 TB 的数据文件，而在此过程中不能导致用户界面 (GUI) 冻结 6。内存瓶颈： 应用程序无法将亿万级的数据点全部加载到 RAM 中，这样做会导致 std::bad_alloc 异常和程序崩溃 8。渲染瓶颈： 即使用户在内存中加载了所有数据，以 60 FPS 的流畅度渲染一亿个数据点也是不现实的。在平移 (Pan) 和缩放 (Zoom) 交互期间，UI 必须保持绝对流畅 4。传统的“加载所有数据 -> 绘制所有数据”的简单模型在这种情况下必然会失败。因此，本方案的架构基石是解耦。应用程序的性能和响应能力，完全取决于将数据 I/O、数据处理（降采样）和UI 渲染三个环节彻底分离的能力 13。我们将设计一个异步的、多线程的、按需加载 (On-Demand) 的架构。在这个模型中，GUI 永远不会直接接触原始数据。相反，它只请求其当前视窗（Viewport）所需的、经过高度降采样的数据子集。所有繁重的 I/O 15 和数据处理 16 任务都将在后台工作线程中完成。1.2 宏观架构：基于 C++ 的模型-视图-控制器 (MVC) 变体为了实现上述解耦，我们将采用一个基于 Qt Model/View 框架 18 的经典模型-视图-控制器 (MVC) 架构变体。Qt 的信号和槽机制天然地促进了数据与表示的分离 19，这对于构建可维护的大型应用至关重要 21。我们的应用程序将由三个核心 C++ 组件构成：模型 (Model) - DataManager：这是一个非 GUI 的 QObject 子类。它将是系统中唯一负责与磁盘上的原始数据文件（例如 Parquet 或 HDF5 文件）进行交互的组件。它将在一个独立的工作线程上运行，以处理所有阻塞型的 I/O 和数据处理操作。视图 (View) - MainWindow 及其子控件：纯粹的 Qt Widgets 组件，包括 QMainWindow、QDockWidget、QTreeView 以及核心的 QCustomPlot 绘图控件。视图的唯一职责是显示数据（无论是信号列表还是绘图）并捕获用户的输入（点击、拖动、缩放）。控制器 (Controller) - PlotController / MainWindow (角色合并)：MainWindow 类本身将充当主要的控制器角色。它负责协调视图和模型。它将监听来自视图的信号（例如 QTreeView::itemChanged、QCPAxis::rangeChanged）和来自模型的信号（例如 dataReady），并管理两者之间的工作流。架构的关键分歧点： Qt 的原生 Model/View 框架（如 QAbstractItemModel）是为 QTreeView、QListView 和 QTableView 等视图设计的 18。然而，高性能绘图库（包括我们推荐的 QCustomPlot）并不原生支持从 QAbstractItemModel 中拉取绘图数据 24。这意味着本架构必须处理两个独立的数据流：元数据流 (Metadata Flow)： DataManager -> QAbstractItemModel -> QTreeView。这用于向用户显示可用的“运行”和“信号”列表。绘图数据流 (Plot Data Flow)： DataManager -> MainWindow (Controller) -> QCPGraph。这用于绘制用户选定的信号数据。这种“断开”是本架构设计的核心。我们不能简单地将一个模型设置给绘图控件。所有的数据加载请求都必须由 MainWindow 控制器层进行明确的“桥接”和管理。第二部分：UI框架实施：构建Simulink Data Inspector风格的界面为了复刻 SDI 的专业外观和灵活布局，我们将完全基于 QMainWindow 的停靠窗口系统。2.1 主窗口与停靠布局应用程序的顶层窗口必须是 QMainWindow。这是唯一支持 QToolBar、QStatusBar 和可停靠 QDockWidget 的 Qt Widgets 类 26。我们将使用 QDockWidget 来创建构成 SDI 界面的各个面板 27。推荐的 UI 布局实现方案：RunBrowserDock (运行浏览器)： 一个 QDockWidget，设置为 Qt::LeftDockWidgetArea。它将包含一个 QTreeView，用于显示加载的多个“运行” (Runs) 1。SignalBrowserDock (信号浏览器)： 另一个 QDockWidget，可以与 RunBrowserDock 标签化堆叠。它包含一个带复选框的 QTreeView 29，用于分层显示所选“运行”中的所有信号（例如，按子系统分层）30。PropertyEditorDock (属性编辑器)： 一个位于右侧的 QDockWidget。它将包含一个 QTableView 或 QPropertyBrowser（一个 Qt 解决方案组件），用于显示和编辑所选信号或绘图的属性（例如，颜色、线型、名称）32。QMainWindow::setCentralWidget 将被设置一个 QWidget，该 QWidget 内部使用 QVBoxLayout 容纳一个 QSplitter，作为主绘图区域。2.2 多图布局：使用 QSplitterSDI 允许用户以多种布局（例如，2x2, 1x3）同时查看多个子图 32。在 Qt Widgets 中，实现这种动态、可由用户调整大小的布局的最佳方式是使用嵌套的 QSplitter 33。MainWindow 的中央控件将包含一个顶层的（例如，垂直的）QSplitter。当用户通过菜单操作“添加绘图”时，应用程序将动态地向这个 QSplitter 中添加一个新的 QCustomPlot 实例。如果用户选择“2x2 布局”，应用程序将创建一个顶层垂直 QSplitter，然后在其内部添加两个水平 QSplitter，每个水平 QSplitter 再包含两个 QCustomPlot 实例。2.3 信号管理：QTreeView 与 QAbstractItemModel这是处理海量信号元数据的关键。如果一个“运行”包含数十万个信号，我们不能在打开它时一次性将它们全部加载到 QTreeView 中，否则 GUI 会冻结 34。解决方案是实现惰性加载 (Lazy Loading)。实现方案：创建一个 SignalTreeModel 类，继承自 QAbstractItemModel。覆盖关键函数： 必须覆盖 fetchMore(const QModelIndex &parent) 和 canFetchMore(const QModelIndex &parent) 34。工作流：当用户在 QTreeView 中展开一个节点 (parent) 时，QTreeView 会调用 canFetchMore(parent)。如果模型知道该节点还有未加载的子项，它返回 true。QTreeView 接着调用 fetchMore(parent)。在 fetchMore 中，模型向 DataManager 线程（参见 3.2 节）发出一个异步元数据请求（例如，"请列出此子系统中的信号"）。DataManager 完成查询后，通过信号将信号列表（QStringList）发回。SignalTreeModel 在其槽函数中接收此列表，调用 beginInsertRows()，将新数据添加到其内部存储中，然后调用 endInsertRows()。QTreeView 会自动更新以显示新加载的项。正如 1.2 节所强调的，这个 SignalTreeModel 绝不存储实际的时间序列数据 24。它的唯一职责是管理信号的元数据（名称、路径、单位以及 Qt::CheckStateRole 的复选框状态 41）。模型中的每个 QModelIndex 将通过 Qt::UserRole 存储一个唯一的 signal_id。当用户勾选一个复选框时 29，控制器 (MainWindow) 会捕获 SignalTreeModel::dataChanged 信号，提取 signal_id，并命令绘图引擎加载并绘制该 ID 对应的数据。第三部分：高性能数据后端：存储、I/O与多线程这是解决“大数据量”问题的核心。3.1 磁盘数据格式选型选择正确的数据存储格式是实现高性能 I/O 的第一步，也是最重要的一步。拒绝的选项： CSV 和 JSON。它们基于文本，解析缓慢，不支持高效的范围查询，不适用于 GB 级数据 43。备选方案 1：HDF5 (Hierarchical Data Format 5)。这是一个非常强大和成熟的科学数据格式 44。它支持分块 (chunking) 和压缩，并允许高效的子集查询 46。这是一个非常可行的选项。备选方案 2：Apache Parquet。这是一个列式存储格式 48。架构决策：强烈推荐 Apache Parquet对于时间序列数据检查器，列式存储在性能上具有压倒性优势。我们的主要查询模式是：“获取信号 S1、S5、S100 在时间 T_start 到 T_end 之间的数据”。在行式存储（如 CSV 或 HDF5 的某些配置）中，如果一个文件包含 1000 个信号，要获取 S1、S5、S100 的数据，系统可能需要读取磁盘上所有 1000 个信号的数据，然后在内存中丢弃 99.7% 的内容。在列式存储 (Parquet) 48 中，每个信号（列）的数据都连续存储在一起。系统可以只读取 "Timestamp" 列、"S1" 列、"S5" 列和 "S100" 列的数据，完全跳过磁盘上所有其他信号的数据块。这使 I/O 效率提高了几个数量级。实现方案：使用 Apache Arrow C++ 库 49 来读写 Parquet 文件。DataManager 将使用 parquet::arrow::FileReader 50 来打开文件，并通过分析 Parquet 文件的元数据（RowGroups 统计信息）来进一步优化，仅读取包含 `` 时间范围的数据块。表 1：后端数据存储格式对比格式 (Format)类型 (Type)C++ 库读性能 (时序范围查询)核心优势Apache Parquet列式 (Columnar)Apache Arrow 50极高仅读取所需列（信号），I/O 开销最小 48。与 Arrow 生态系统完美集成。HDF5行式/分块 (Row/Chunked)HDF5 Library 45高成熟的科学标准，支持复杂数据结构和分块查询 47。自定义二进制行式 (Row-based)N/A (Custom)中格式可控，但需要自行实现所有索引和查询逻辑。CSV / JSON文本 (Text)N/A (Text Parsing)极低不可接受。需要完整解析，无法进行高效的范围查询 43。3.2 异步数据访问架构我们已经确定 I/O 必须在 GUI 线程之外进行 6。在 Qt 中实现这一点的最佳实践是使用 moveToThread 模式 51。绝对不要通过子类化 QThread 并重写 run() 来执行工作 54。这种方式是反模式的，它将线程与特定任务紧密耦合。正确的 moveToThread 实现方案：创建一个 DataManager 类，继承自 QObject。不要让它继承 QThread。在 DataManager.h 中定义所有的数据处理函数作为 public slots:，例如 processDataRequest(QString signal_id, QCPRange range)。在 DataManager 中定义信号，例如 dataProcessed(QString signal_id, QVector<QCPData> data)。在 MainWindow 的构造函数中（即 GUI 线程中）设置工作线程：C++// 在 MainWindow 的构造函数中
dataManagerThread = new QThread(this); // 创建一个新线程
dataManager = new DataManager();      // 创建数据管理器（无父对象）
dataManager->moveToThread(dataManagerThread); // 将 dataManager 移动到新线程

// 连接信号槽，用于异步请求和回调
// 1. [GUI 线程] -> [工作线程]
connect(this, &MainWindow::requestDataRange, 
        dataManager, &DataManager::processDataRequest, 
        Qt::QueuedConnection);

// 2. [工作线程] -> [GUI 线程]
connect(dataManager, &DataManager::dataProcessed, 
        this, &MainWindow::onDataReceived, 
        Qt::QueuedConnection);

// 启动线程的事件循环
dataManagerThread->start();
这种架构模式（52）是 Qt 多线程的基石。它创建了一个拥有自己事件循环的工作线程。dataManager 对象现在“存活”在该工作线程中。当 MainWindow 发出 requestDataRange 信号时，该信号（以及其参数）会被排入工作线程的事件队列。工作线程的事件循环会拾取该事件，并调用 dataManager 的 processDataRequest 槽函数。由于 processDataRequest 是在工作线程中执行的，它可以安全地执行任意长时间的阻塞操作（如读取 10GB 的 Parquet 文件 50 或运行复杂的降采样算法），而 GUI 线程则保持 100% 的响应。完成后，dataManager 发出 dataProcessed 信号，Qt 的信号槽系统会安全地将该信号（及其 QVector<QCPData> 负载）排队返回给 GUI 线程，触发 MainWindow::onDataReceived 槽函数。3.3 内存数据模型如 2.3 节所述，SignalTreeModel 必须支持惰性加载 34。canFetchMore/fetchMore 38 的实现将依赖于 3.2 节的异步架构。当 fetchMore 被 QTreeView 调用时，它不会自己去读文件。它会向 DataManager 线程发出一个元数据请求（例如，requestSignalList(QString group_path)）。DataManager 异步地从 Parquet/HDF5 文件中读取元数据（信号名称列表），并通过信号将 QStringList 返回给 SignalTreeModel。SignalTreeModel 在其槽函数中接收此列表，并使用 beginInsertRows / endInsertRows 更新视图。第四部分：核心绘图引擎：实现亿级数据点的流畅可视化4.1 关键库选择：QCustomPlot (QCP)选择一个高性能的绘图库是本项目的另一个关键决策。我们只考虑 C++/Qt Widgets 库。Qt Charts (原生)： 不推荐。社区和基准测试普遍反映，Qt Charts 在处理超过几万个数据点时性能会急剧下降 55。其 append() API 效率低下 58，即使开启 OpenGL 加速 59，也无法满足“大数据量”的需求。Qwt (Qt Widgets for Technical Applications)： 一个强大、成熟且非常可行的选项 60。Qwt 性能极高，尤其是在平移和缩放操作上，优于 QCustomPlot 11。它被广泛用于需要处理数百万个点的科学应用中 4。QCustomPlot (QCP)： 本方案的推荐选项。 QCP 专门为高性能实时可视化和美观的发布质量图表而设计 14。性能： 它具有高度优化的渲染管线，并内置了 setOpenGl(true) 一键切换 OpenGL 硬件加速的功能 61。关键特性： 它内置了自适应采样 (Adaptive Sampling) 10，这是处理大数据的第一道防线。易用性： QCP 的 API 非常简洁，文档和示例极其出色，并且它被编译为单个头文件和源文件，易于集成。许可： 默认为 GPL，但也提供清晰的商业许可选项 14。Qt Graphs (Qt 6)： 这是一个基于 Qt 6 RHI (渲染硬件接口) 的较新模块 63。它具有极高的性能，但主要面向 QML (QtQuickPlotScene 65) 和 3D 可视化 66，不适用于我们基于 Qt Widgets 的 2D 检查器架构。决策： QCustomPlot。虽然 Qwt 在原始平移性能上可能略占优势 11，但 QCustomPlot 在易用性、美观、内置特性（自适应采样、OpenGL切换）和现代 API 之间提供了最佳的综合平衡。表 2：C++/Qt 绘图库选型对比库 (Library)核心技术性能 (大数据量)关键特性许可证推荐度QCustomPlotQt Widgets (QPainter)非常高自适应采样 10, OpenGL 加速 61, 易于集成GPL / 商业最高QwtQt Widgets (QPainter)极高成熟, 稳定, 性能极强 11, 缩放平移流畅LGPL高Qt ChartsQt Graphics View差易于使用, 与 Qt 集成GPL / 商业不推荐 56Qt Graphs (Qt 6)Qt Quick / RHI极高RHI GPU 加速 64, 3D 66GPL / 商业不适用 (QML/3D) 654.2 数据降采样 (Downsampling) 算法这是本方案的核心技术。我们无法将 1 亿个点绘制到 2000 像素宽的屏幕上 10。因此，我们必须在将数据发送到绘图控件之前对其进行降采样。错误的方法：平均 (Averaging)： 会将所有尖锐的峰值和谷值“平滑”掉，导致数据失真。工程师关心的异常信号将丢失 69。抽取 (Decimation)： 即“每 N 个点取一个” 70。这几乎可以保证丢失所有瞬时峰值和毛刺，在工程分析中是不可接受的 71。正确的方法：保留视觉保真度的算法Min-Max 算法：原理： 将可见的 X 轴（时间）范围划分为 N 个桶（Buckets），N 通常等于绘图区域的像素宽度。对于每个桶，算法只保留该桶内的最小值和最大值两个点 73。优势： 这保证了数据的包络线 (Envelope) 是视觉上准确的。用户绝不会丢失任何峰值或谷值 75。LTTB (Largest-Triangle-Three-Buckets)：原理： 同样是分桶。但它不是选择 Min/Max，而是选择能与前一个桶的选定点构成最大面积三角形的点 76。优势： 这种方法能极好地保留数据的视觉形态 (visual shape)，使其看起来与原始曲线非常相似 79。有现成的 C++ 实现 80。架构决策：为“检查器”选择 Min-Max，而非 LTTBLTTB 的目标是让降采样后的图表看起来像原始图表 79。Min-Max 的目标是确保原始数据的所有极值都被忠实表示 72。我们的目标是构建一个 "Data Inspector"（数据检查器）。对于一个正在调试模型的工程师来说，丢失一个孤立的、但可能是关键的异常峰值是不可接受的。LTTB 可能会为了保留整体曲线的“形态”而丢弃一个孤立的峰值。而 Min-Max 算法保证会保留这个峰值。因此，对于分析和调试工具，Min-Max 算法在功能上优于 LTTB。它也更易于实现，并且可以高度并行化（每个桶的 Min-Max 计算都是独立的）16。表 3：高性能降采样算法对比算法 (Algorithm)原理 (Principle)速度 (Speed)峰值保留 (Peak Preservation)推荐度 (用于分析)Min-Max保留每个桶内的最小值和最大值 74极快完美 (保留所有极值) 72最高LTTB保留构成最大三角形的点 76快良好 (保留视觉形态) 79高 (用于美观)平均 (Average)计算每个桶的平均值极快差 (平滑峰值) 69不推荐抽取 (Decimation)每 N 个点取一个 70最快极差 (丢失峰值) 71绝对禁止Min-Max C++ 核心实现（在 DataManager 工作线程中）：C++/**
 * @brief 对原始数据进行 Min-Max 降采样
 * @param rawData 原始信号数据（必须支持高效的范围查询）
 * @param visibleMinX 可见范围的 X 轴最小值
 * @param visibleMaxX 可见范围的 X 轴最大值
 * @param targetPoints 目标点数（例如，绘图区域的像素宽度）
 * @return 降采样后的数据点
 */
QVector<QCPData> DownsampleMinMax(
    const RawSignalStorage& rawData, 
    double visibleMinX, 
    double visibleMaxX, 
    int targetPoints) 
{
    //  我们将生成 targetPoints 个桶，每个桶 2 个点 (min, max)
    QVector<QCPData> downsampledData;
    downsampledData.reserve(targetPoints * 2); 

    const double bucketSize = (visibleMaxX - visibleMinX) / targetPoints;
    if (bucketSize <= 0) {
        return downsampledData; // 范围无效
    }

    // [16, 73] 可以使用 OpenMP 或 QtConcurrent 并行处理所有桶
    // 这里为清晰起见显示串行实现
    for (int i = 0; i < targetPoints; ++i) {
        const double bucketStart = visibleMinX + i * bucketSize;
        const double bucketEnd = bucketStart + bucketSize;

        //  在桶内查找 min/max。这是算法的核心。
        double minVal = std::numeric_limits<double>::max();
        double maxVal = -std::numeric_limits<double>::max();
        double minKey = bucketStart;
        double maxKey = bucketStart;
        bool bucketHasData = false;

        // 关键优化：必须从原始数据中高效地获取此范围的迭代器
        // 而不是每次都从头扫描
        auto it_start = rawData.findLowerBound(bucketStart);
        auto it_end = rawData.findUpperBound(bucketEnd);

        for (auto it = it_start; it!= it_end; ++it) {
            bucketHasData = true;
            const double& value = it.value();
            if (value < minVal) {
                minVal = value;
                minKey = it.key();
            }
            if (value > maxVal) {
                maxVal = value;
                maxKey = it.key();
            }
        }

        if (bucketHasData) {
            //  按 X 轴顺序添加 min 和 max 点
            if (minKey < maxKey) {
                downsampledData.append(QCPData(minKey, minVal));
                downsampledData.append(QCPData(maxKey, maxVal));
            } else if (maxKey < minKey) {
                downsampledData.append(QCPData(maxKey, maxVal));
                downsampledData.append(QCPData(minKey, minVal));
            } else {
                // Min 和 Max 是同一个点
                downsampledData.append(QCPData(minKey, minVal));
            }
        }
    }
    return downsampledData;
}
4.3 动态数据加载与渲染管线这是将 3.2 节的异步架构与 4.2 节的降采样算法粘合在一起的渲染管线。它完全由用户的平移和缩放操作驱动。实现方案：rangeChanged 信号槽机制 83连接信号 (GUI 线程)： 在 MainWindow 中，连接 QCustomPlot 实例的 X 轴信号：C++connect(customPlot->xAxis, SIGNAL(rangeChanged(QCPRange)), 
        this, SLOT(onAxisRangeChanged(QCPRange)));
83（注意：为清晰起见，此处使用 Qt 4 语法，QCP 示例中常用。推荐使用 Qt 5 的函数指针语法。）去抖动 (Debouncing) (GUI 线程)： 用户在拖动时会发出数百个 rangeChanged 信号。我们不能为每个信号都触发一次昂贵的数据查询。因此，我们使用一个 QTimer 来实现“去抖动”：C++// 在 MainWindow.h 中
QTimer* m_updateTimer; 

// 在 MainWindow.cpp 构造函数中
m_updateTimer = new QTimer(this);
m_updateTimer->setInterval(100); // 100ms 延迟
m_updateTimer->setSingleShot(true);
connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::triggerDataUpdate);

// 在槽函数中
void MainWindow::onAxisRangeChanged(const QCPRange &newRange) {
    m_updateTimer->start(); // 重置计时器
}
触发更新 (GUI 线程)： 当用户停止拖动 100ms 后，m_updateTimer 最终触发 triggerDataUpdate 槽：C++void MainWindow::triggerDataUpdate() {
    QCPRange visibleRange = customPlot->xAxis->range();
    int pixelWidth = customPlot->xAxis->axisRect()->width();

    // 获取所有当前选中的 signal_id
    QStringList activeSignals = m_signalTreeModel->getActiveSignalIDs(); 

    // [52] 向工作线程发出异步请求
    for (const QString& id : activeSignals) {
        emit requestDataRange(id, visibleRange, pixelWidth);
    }
}
处理与降采样 (工作线程)： DataManager 的 processDataRequest 槽函数被调用。它执行 3.1 节的 Parquet I/O 和 4.2 节的 Min-Max 降采样。完成后，它发出信号：emit dataProcessed(id, downsampledData);。接收与渲染 (GUI 线程)： MainWindow 的 onDataReceived 槽被调用：C++void MainWindow::onDataReceived(QString signal_id, QVector<QCPData> data) {
    //  查找该信号对应的 QCPGraph
    QCPGraph* graph = m_signalGraphMap.value(signal_id); 
    if (graph) {
        graph->setData(data); // 设置数据
        customPlot->replot(QCustomPlot::rpQueuedReplot); // 请求重绘 [13]
    }
}
架构优势 (规避 QCP 瓶颈)：社区中大量讨论指出 QCPGraph::setData 在处理数百万个点时性能不佳，因为其内部数据结构是 QCPDataMap (一个 QMap) 88。在我们的“按需加载”架构中，这个问题被完全规避了。我们永远不会给 setData 传递 1 亿个点。我们只传递降采样后的数据（例如，targetPoints * 2，大约 2000-4000 个点）。将 4000 个点批量插入到 QMap 中的开销是完全可以忽略不计的 88。因此，我们利用了 QCP 的便利性，同时完美地绕过了它唯一的（已知的）大规模数据性能瓶颈。4.4 GPU 加速QCustomPlot 提供了简单的一键式 OpenGL 硬件加速 61。实现： 在创建 QCustomPlot 实例后，只需调用 customPlot->setOpenGl(true);。注意： 这不一定是“银弹”。一些报告指出，在某些情况下，软件渲染器可能更快 91。OpenGL 加速主要优化的是绘制 (drawing) 本身，例如绘制粗线、抗锯齿和半透明填充 61。在我们的架构中，性能瓶颈很可能在 CPU 端（I/O 和降采样）。尽管如此，鉴于其实现简单，建议启用并进行性能评测。第五部分：实现SDI的标志性交互功能5.1 交互式信号管理 (QTreeView -> QCPGraph)此功能将 2.3 节的 QTreeView 与 4.1 节的 QCustomPlot 连接起来。模型设置： SignalTreeModel 必须在 flags() 中返回 Qt::ItemIsUserCheckable 41，并在 data() / setData() 中正确处理 Qt::CheckStateRole 29。控制器连接： MainWindow 连接 SignalTreeModel::dataChanged 信号。槽函数 onModelDataChanged(const QModelIndex &topLeft,...)：检查 role == Qt::CheckStateRole。获取被更改项的 signal_id (存储在 Qt::UserRole 中)。如果被勾选 (Checked)：a.  QCPGraph *graph = customPlot->addGraph(); 83b.  设置图表样式（例如，从颜色池中分配颜色）。c.  将 signal_id 和 graph 存储在一个 QMap<QString, QCPGraph*> 中，以便将来查找。d.  调用 triggerDataUpdate() (见 4.3 节) 来加载并绘制该图表的数据。如果被取消勾选 (Unchecked)：a.  从 QMap 中查找 signal_id 对应的 graph。b.  调用 customPlot->removeGraph(graph) 92。c.  从 QMap 中移除该条目。d.  调用 customPlot->replot()。5.2 高级数据游标 (Crosshairs)这是 SDI 的核心分析功能：一个（或两个 32）垂直游标，它会同步显示所有可见信号在当前 X (时间) 坐标上的插值 Y 值 32。在 QCustomPlot 中实现这一点的关键是使用 QCPItemTracer。实现方案：创建游标项 (GUI 线程)： 在 MainWindow 中，创建游标的可视化组件。创建一个 QCPItemLine 作为垂直线 96。创建一个 QCPItemText 作为显示值的主标签 97。为每个图表创建 Tracer： 我们需要一个 QCPItemTracer 来计算每个图表上的值。创建一个 QMap<QCPGraph*, QCPItemTracer*> 映射。当 addGraph (5.1 节) 时，同时创建一个 QCPItemTracer 并存入此映射 98。跟踪鼠标 (GUI 线程)： 连接 QCustomPlot::mouseMove 信号 100。onCursorMouseMove(QMouseEvent *event) 槽 (GUI 线程)：a.  获取鼠标X坐标：double key = customPlot->xAxis->pixelToCoord(event->pos().x()); 100。b.  更新垂直线位置：verticalLine->point1->setCoords(key, customPlot->yAxis->range().lower); 和 verticalLine->point2->setCoords(key, QCPRange::maxRange); 96。c.  遍历所有可见图表并更新 Tracer： 这是此功能的核心。```cpp
QString cursorText = QString("Time: %1\n").arg(key);

// 遍历所有 QCPGraph 和它们关联的 QCPItemTracer
for(auto it = m_graphTracerMap.begin(); it!= m_graphTracerMap.end(); ++it) {
    QCPGraph *graph = it.key();
    QCPItemTracer *tracer = it.value();

    if (graph->visible()) {
        tracer->setVisible(true);

        //  这三行是关键：
        tracer->setGraph(graph);        // 1. 关联图表
        tracer->setGraphKey(key);       // 2. 设置X坐标
        tracer->setInterpolating(true); // 3. 启用插值

        // [97, 103] 自动获取插值后的Y值
        double value = tracer->position->value(); 

        // 更新游标标签文本
        cursorText += QString("%1: %2\n").arg(graph->name()).arg(value);
    } else {
        tracer->setVisible(false); // 隐藏未显示图表的 tracer
    }
}

// 更新主 QCPItemText 标签
m_cursorTextLabel->setText(cursorText);
m_cursorTextLabel->position->setCoords(key,...); // 放在游标附近

customPlot->replot(QCustomPlot::rpQueuedReplot);
```
QCPItemTracer 98 结合 setGraphKey 和 setInterpolating(true)，为我们处理了在两个原始数据点之间进行线性插值以找到任意 key 处 value 的所有复杂数学运算。这使得 SDI 风格的游标实现变得非常简单。5.3 多运行数据比较SDI 的另一个标志性功能是比较两次“运行” (Runs) 的同名信号，并绘制它们之间的差异 (RunA.Sig1 - RunB.Sig1) 2。这是一个数据处理任务，必须在 DataManager 工作线程中执行。实现方案：UI 扩展： RunBrowserDock (见 2.1 节) 的 UI 需要允许用户右键点击一个“运行”，并将其设置为“基准”(Baseline)。然后再右键点击另一个“运行”，并设置为“比较”(Compare to Baseline) 1。模型扩展： 当设置了比较时，SignalTreeModel (见 2.3 节) 应被触发刷新。它现在会动态地在 QTreeView 中为每个同时存在于两个运行中的信号创建“差异”节点（例如，"Signal A (Diff)"）。数据请求： 当用户勾选这个“差异”节点时，MainWindow (控制器) 会向 DataManager 线程发出一个新的异步信号，例如 requestDifferenceData(baseline_id, compare_id, visibleRange, pixelWidth)。后端处理 (DataManager 工作线程)： DataManager 的 processDifferenceData 槽函数执行：a.  异步加载两个信号（baseline_id 和 compare_id）的原始数据（不是已降采样的数据），范围为 visibleRange。b.  对齐： 由于两次运行的时间戳向量 (key) 可能不同，必须将“比较”信号重采样（通过线性插值）到“基准”信号的时间戳向量上。c.  计算差异： 创建一个新的（临时的）信号 QVector<QCPData>，其 value 是两个信号插值后的 value 之差。d.  降采样： 将这个新的差异信号向量传递给 4.2 节中的 DownsampleMinMax 算法 74。e.  通过 dataProcessed 信号将降采样后的差异数据发回 GUI 线程。渲染 (GUI 线程)： MainWindow 的 onDataReceived 槽接收此数据，并将其 setData 到一个新的、表示差异的 QCPGraph 中。第六部分：总结与实施路线图6.1 关键架构决策总结本方案旨在构建一个高性能、可扩展的 C++/Qt 数据检查器。其核心架构决策总结如下：UI 框架： QMainWindow + QDockWidget + QSplitter。提供 SDI 风格的灵活、专业的停靠式布局 26。信号列表： QTreeView + 自定义 QAbstractItemModel。通过覆盖 fetchMore 实现信号元数据的惰性加载，以处理海量信号列表 34。绘图库： QCustomPlot。因其卓越的性能、现代化的 API、内置的 OpenGL 支持 61 和自适应采样功能而被选中 14。数据后端： Apache Parquet (通过 Apache Arrow C++)。其列式存储特性 48 是实现高效时间序列范围查询 I/O 的关键 50。核心架构： 按需加载 (On-Demand)。UI 永远不接触原始数据。QCPAxis::rangeChanged 信号 83 驱动一个异步管线，仅加载和绘制当前视窗所需的降采样数据 84。多线程： moveToThread 模式 52。将所有 I/O 和数据处理（降采样）任务隔离在 DataManager 工作线程中，保证 GUI 绝对流畅 6。核心算法： Min-Max 降采样。在工作线程中执行，以保证在不丢失任何峰值或谷值（数据包络线）72 的前提下，将数据量减少几个数量级。6.2 实施路线图建议按以下里程碑顺序开发此复杂应用程序：里程碑 1 (UI 骨架)： 构建 QMainWindow、QDockWidget 和 QSplitter 布局。在中央控件中添加一个 QCustomPlot 实例。使其能够加载一个硬编码的 QVector 数据并成功绘图 83。里程碑 2 (后端线程)： 实现 DataManager 类和 moveToThread 架构 (如 3.2 节所示) 52。建立 requestData / dataReady 信号槽连接。测试从工作线程向 GUI 线程异步发送一个硬编码的 QVector<QCPData> 并绘制。里程碑 3 (按需渲染管线)： 连接 QCPAxis::rangeChanged 信号 83，通过去抖动 QTimer，触发对 DataManager 的异步请求 84。在 DataManager 中实现存根(stub)数据请求（例如，根据请求的 QCPRange 返回一个计算生成的正弦波）。验证平移/缩放是否能触发异步数据更新。里程碑 4 (核心算法)： 在 DataManager 的工作线程中实现 4.2 节中的 Min-Max 降采样算法 74。实现一个加载大型（例如 5000 万点）硬编码 QVector 的测试，并验证 onAxisRangeChanged 现在只返回降采样后的数据（~4000 点）。里程碑 5 (真实 I/O)： 将 DataManager 中的存根数据替换为真实的 Apache Arrow / Parquet 50 或 HDF5 45 文件读取器。实现高效的范围查询。里程碑 6 (SDI 功能 - 信号列表)： 实现 2.3 节中的 SignalTreeModel，包括 fetchMore 惰性加载 38 和复选框 (Qt::CheckStateRole 29)。实现 5.1 节中的控制器逻辑，将复选框点击连接到 addGraph / removeGraph 92。里程碑 7 (SDI 功能 - 游标与比较)： 实现 5.2 节中的 QCPItemTracer 游标管理器 98 和 5.3 节中的多运行数据比较与差异计算逻辑。遵循此架构和路线图，将能够构建一个在性能和功能上均可对标 Simulink Data Inspector 的专业级 C++/Qt 应用程序。