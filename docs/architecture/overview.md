# 整体架构

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联代码**：全仓库 · **配套**：[渲染管线](render-pipeline.md)、[数据与导出](data-and-export.md)、[并发](concurrency.md)

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
└──────────────────────────────────────────────────────────┘
```

## 数据流

1. `AppController` 在工作线程创建 `DataLoadWorker`，串行读取文件、解析、构建分块极值索引。
2. 结果通过 `LoadedTable` 值类型投递给 GUI 线程，`PlotSeriesStore` 持有不可变原始序列和索引，子图只存系列 ID。
3. 视图变化时 `PlotItem` 把请求交给 `PlotLodScheduler`（最多两个后台线程），生成结构化 LOD。
4. `PlotGeometryBuilder` 把 LOD 转成裁剪后的屏幕空间三角带，由 `PlotItem::updatePaintNode` 提交给 Scene Graph。
5. 导出时 `DataExportWorker` 从 Store 取不可变快照，后台写 XLSX/MAT。

## 线程模型

详见 [并发与线程安全](concurrency.md)。一句话：GUI 线程处理界面与视图交互，Scene Graph 渲染线程提交节点；加载、索引构建、LOD 生成、导出写盘都在后台线程；跨线程共享的数据一律不可变快照。

## PlotItem 实现分工

- `plotitem.cpp`：生命周期、不可变快照、视图范围与刻度。
- `plotitem_cursor.cpp`：原始样本游标导航与读数；只保留双游标位置和供 QML 使用的读数列表。
- `plotitem_interaction.cpp`：曲线拾取、平移、滚轮缩放和游标拖动。
- `plotitem_render.cpp`：渲染线程拥有的 QSG 节点、几何上传与曲线排序，使用 Qt 内置 `QSGFlatColorMaterial`。

这些文件共同实现同一个 `PlotItem`，状态仍由 `m_dataMutex` 保护，不增加跨线程可变对象。

## 二维/三维航迹子图

- `AppController::m_trajectories` 独立保存每个子图的模式、XYZ 绑定和相机状态；`appcontroller_trajectory.cpp` 负责生命周期、信号行号重映射和时间游标联动。
- `TrajectoryPlot.qml` 在航迹模式下由 Loader 创建，覆盖原时间序列视图。隐藏的 PlotItem 仍接收共享时间范围及原始 XYZ 系列，作为时间游标/键盘步进端点；原时间图绑定单独保留。
- `render/trajectorybuilder.*` 验证已选两/三轴的时间基、提取有效样本段，缓存归一化位置和多级空间简化索引。预览按当前投影范围适应画布，复用索引和每请求一次的投影矩阵；缺口必须断线，游标仍访问原始样本。
- `trajectoryitem.*` 使用最多两个轨迹工作线程生成不可变预览，GUI 线程只提交相机状态；渲染线程复用 QSG 节点。
- SessionPlot 保存轨迹来源、相机和坐标类型；v5 保存子图可用信号，轴映射和相机保持独立，兼容 v1–v4。来源 ID 在会话恢复及删除文件时重映射。
- 经纬度模式后台缓存 WGS84 局部北/东/向下记录高度差（NED）；原始 Store 不变，游标仍定位原始样本。固定屏幕大小的方向坐标轴取代外部坐标框。
- 两参数自动成为 XY/XZ/YZ 平面航迹，经纬度允许高度留空。QML 保留顶部一排绑定控件和底部一行读数，CAD 三轴旋转环合并到左下方向坐标轴，靠近展开、拖动保持、离开收起；拖环约束世界轴，角落环内自由旋转，画布或 Shift+左键平移。旋转支点仍为子图中心，仅旋转时显示中央小十字，角度反馈位于角落。按钮“时间图”切回原时间序列。
- 航迹视图直接打开，信号树管理每个子图独立的可用信号列表，新航迹默认经纬度模式；新选信号按选入顺序填充第一个空轴，顶部 ComboBox 可调整轴来源。SignalModel 的活动选择覆盖仅显示可用列表，不改变保留的时间图绑定；切换子图、配置、删除来源和会话恢复统一同步。
- 取消轴绑定保留可用信号；从树移除来源清空其轴映射；跨轴重复选择交换两轴。首次进入时已有时间图信号按列表顺序填充空轴；三轴满后仅添加候选，不替换绑定。不按名称猜测轴角色，无临时轴目标。左下方向坐标轴和旋转环共用原点，角落旋转环优先命中，窗口事件回归检查实际 QML 交互链。
