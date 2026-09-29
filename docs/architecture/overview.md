# 整体架构

DataInspector 从老的 QCustomPlot/QWidget 实现（`src/core`、`src/plot`、`src/ui`、`src/script`、`src/data`，已在提交 `8d2b6bb` 后移除）切换到 Qt 6.8 Qt Quick Scene Graph 渲染。

## 模块划分

```
┌──────────────────────────────────────────────────────────┐
│  QML 界面层                                               │
│  qml/Main.qml         主窗口、工具栏、信号树、子图网格     │
│  qml/QuickPlot.qml    单个子图：坐标轴、游标、右键菜单     │
│  qml/PlotLegend.qml   图例 Flow、拖拽、菜单                │
│  qml/PlotAxisArea.qml 轴留白区悬停/框选/滚轮              │
│  qml/Splash.qml       启动画面                            │
└──────────────┬───────────────────────────────────────────┘
               │ Q_PROPERTY / Q_INVOKABLE
┌──────────────▼───────────────────────────────────────────┐
│  控制器层  src/quick/appcontroller.*                       │
│  appcontroller.cpp            生命周期、工作线程、状态     │
│  appcontroller_loading.cpp    文件加载/移除、导出入口     │
│  appcontroller_plots.cpp      子图绑定、布局、自适应      │
│  appcontroller_session.cpp    会话收集、事务式恢复        │
│  sessiondocument.*            会话 JSON 校验与原子读写    │
│  signalmodel.*                QML 信号树模型              │
│  signalmetadata.*             信号元数据                  │
│  startupfiles.*               命令行参数解析              │
└──────────────┬───────────────────────────────────────────┘
               │
┌──────────────▼───────────────────────────────────────────┐
│  数据与导出                                               │
│  dataloadworker.*   后台加载调度、CSV/TXT/MAT 解析        │
│  xlsxreader.*       XLSX 解析                             │
│  dataexportworker.* 后台导出调度、进度、取消              │
│  xlsxwriter.*       XLSX 写入、工作表拆分                 │
│  matwriter.* / mat5streamwriter.*  MAT5 流式写入         │
│  exporttable.h / exportvalidation.cpp 导出共用结构与校验 │
└──────────────┬───────────────────────────────────────────┘
               │
┌──────────────▼───────────────────────────────────────────┐
│  渲染核心  src/quick/render/                              │
│  plotseriesstore.*   不可变原始序列 Store、代际号         │
│  plotrangeindex.*    512 样本分块极值 + 线段树           │
│  plotlodbuilder.*    视窗/屏幕桶 min/max LOD              │
│  plotlodscheduler.* 最多两个后台线程调度、合并请求        │
│  plotgeometrybuilder.* LOD → 屏幕空间三角带               │
│  plotaxisutils.*     浮点刻度生成、适应边距               │
└──────────────┬───────────────────────────────────────────┘
               │
┌──────────────▼───────────────────────────────────────────┐
│  Scene Graph 项  src/quick/plotitem.*                     │
│  视图状态、交互、QSG 节点提交、游标、归一化               │
│  plotblendmaterial.*  flatcolor 材质与混合方程             │
└──────────────────────────────────────────────────────────┘
```

## 数据流

1. `AppController` 在工作线程创建 `DataLoadWorker`，串行读取文件、解析、构建分块极值索引。
2. 结果通过 `LoadedTable` 值类型投递给 GUI 线程，`PlotSeriesStore` 持有不可变原始序列和索引，子图只存系列 ID。
3. 视图变化时 `PlotItem` 把请求交给 `PlotLodScheduler`（最多两个后台线程），生成结构化 LOD。
4. `PlotGeometryBuilder` 把 LOD 转成裁剪后的屏幕空间三角带，由 `PlotItem::updatePaintNode` 提交给 Scene Graph。
5. 导出时 `DataExportWorker` 从 Store 取不可变快照，后台写 XLSX/MAT。

## 线程模型

详见 [并发与线程安全](concurrency.md)。一句话：GUI 线程只做视图交互和节点提交；加载、索引构建、LOD 生成、导出写盘都在后台线程；跨线程共享的数据一律不可变快照。
