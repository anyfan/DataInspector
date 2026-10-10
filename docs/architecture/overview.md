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

关于页面由 `AboutDialog.qml` 展示，`AppController::aboutInfo`/`releaseNotes` 提供常量信息，`copyAboutInfo` 在 GUI 线程写入剪贴板。`appcontroller_about.cpp` 读取构建时生成的元数据和嵌入的更新记录，不在程序启动时查询 Git，也不依赖源码目录。

1. `AppController` 在工作线程创建 `DataLoadWorker`，串行读取文件、解析、构建分块极值索引。
2. 结果通过 `LoadedTable` 值类型投递给 GUI 线程，`PlotSeriesStore` 持有不可变原始序列和索引，子图只存系列 ID。
3. 视图变化时 `PlotItem` 把请求交给 `PlotLodScheduler`（最多两个后台线程），生成结构化 LOD。
4. `PlotGeometryBuilder` 把 LOD 转成裁剪后的屏幕空间三角带，由 `PlotItem::updatePaintNode` 提交给 Scene Graph。
5. 导出时 `DataExportWorker` 从 Store 取不可变快照，后台写 XLSX/MAT。

信号树内部拖拽由 Main.qml 在 GUI 线程管理预览与可见子图命中，复用 QuickPlot 的目标边框高亮。松开调用 AppController::addSignalToPlot 校验源/目标并激活目标，通过现有 selectSignal 添加时间图绑定或航迹候选；重复添加保持幂等，模型重置清理拖拽状态。它独立于窗口外部文件拖放，不修改 Store 或线程模型。

信号树在数据来源变化时一次扫描建立持久分组行/祖先索引，过滤复用索引并扫描名称，避免逐组全量扫描和逐信号反复拆分路径。UI 搜索延迟 150ms 合并输入；清空搜索恢复过滤前的滚动位置。拖拽手势使用修订号隔离取消，Esc、窗口失焦或布局变化后同一按压不能重新启动拖拽。

会话修改状态在控制器集中追踪绑定、样式、数据来源、视图、游标及相机变化；保存/成功恢复后清理，重定位恢复后保持未保存。关闭和替换会话时由 QML 提供保存/放弃/取消。图片导出入口在 `appcontroller_image.cpp`，不改变原始数据或会话格式。

SignalModel 的组展开/折叠使用局部行增删及展开角色通知，保留未受影响节点的持久索引与 ListView 浏览位置；深层节点检查全部祖先展开状态。单行路径导航覆盖在固定视窗上，不通过显隐改变列表或滚动条轨道几何。名称拖拽从左键按下起禁止父 Flickable 抢占手势，滚轮及滚动条浏览独立保留。

## 线程模型

详见 [并发与线程安全](concurrency.md)。一句话：GUI 线程处理界面与视图交互，Scene Graph 渲染线程提交节点；加载、索引构建、LOD 生成、导出写盘都在后台线程；跨线程共享的数据一律不可变快照。

## PlotItem 实现分工

- `plotitem.cpp`：生命周期、不可变快照、视图范围与刻度。
- `plotitem_cursor.cpp`：原始样本游标导航与读数；只保留双游标位置和供 QML 使用的读数列表。
- `plotitem_interaction.cpp`：曲线拾取、平移、滚轮缩放和游标拖动。
- `plotitem_render.cpp`：渲染线程拥有的 QSG 节点、几何上传与曲线排序，使用 Qt 内置 `QSGFlatColorMaterial`。

这些文件共同实现同一个 `PlotItem`，状态仍由 `m_dataMutex` 保护，不增加跨线程可变对象。

游标键盘目标由 PlotItem 的 activeCursorIndex 保存，在锁内读写，独立 activeCursorChanged 通知避免仅选择游标触发位置同步。1/2 表示单个目标，0 表示两个目标；点击/拖动线选择目标，拖过另一线时连同操作目标交接。stepCursor 默认移动选中目标；选中两个时根据第一条线相邻原始样本的时间差同步平移，moveCursorPair 原子发布两个位置，保持间距并在原先都可见时整体裁剪到视窗边界；显式 index=0 保留分别步进的内部入口。单转双只在活动视窗内初始化 T2，位置同步与会话恢复仍使用精确状态。QML 的 X 轴留白固定为 22 像素，三个数字示数紧贴轴线，按实际宽度水平避让和省略过长文字；仅一条线可见时隐藏差值。Text 内 MouseArea 用固定按下坐标和时间快照拖动，时间示数选择单条，差值选择两条，轴其他留白仍负责框选；示数悬停不显示提示或特殊鼠标图标。

## 二维/三维航迹子图

- `AppController::m_trajectories` 独立保存每个子图的模式、航迹列表、活动项、子图共用候选与相机状态；`appcontroller_trajectory.cpp` 负责生命周期、信号行号重映射和时间游标联动。
- `TrajectoryPlot.qml` 在航迹模式下由 Loader 创建，覆盖原时间序列视图。隐藏的 PlotItem 仍接收共享时间范围及原始 XYZ 系列，作为时间游标/键盘步进端点；原时间图绑定单独保留。
- `render/trajectorybuilder.*` 验证已选两/三轴的时间基、提取有效样本段，缓存归一化位置和多级空间简化索引。预览按当前投影范围适应画布，复用索引和每请求一次的投影矩阵；缺口必须断线，游标仍访问原始样本。
- `trajectoryitem.*` 使用最多两个轨迹工作线程生成不可变预览，GUI 线程只提交相机状态；渲染线程复用 QSG 节点。
- SessionPlot 保存轨迹来源、相机和坐标类型；v6 保存独立航迹及姿态绑定、样式和子图共用相机，兼容 v1–v5 单航迹。来源 ID 在会话恢复及删除文件时重映射。
- 经纬度模式后台缓存 WGS84 局部北/东/向下记录高度差（NED）；原始 Store 不变，游标仍定位原始样本。固定屏幕大小的方向坐标轴取代外部坐标框。
- 两参数自动成为 XY/XZ/YZ 平面航迹，经纬度允许高度留空。QML 保留顶部两排管理/绑定控件和底部一行读数，CAD 三轴旋转环合并到左下方向坐标轴，靠近展开、拖动保持、离开收起；拖环约束世界轴，角落环内自由旋转，画布或 Shift+左键平移。旋转支点仍为子图中心，仅旋转时显示中央小十字，角度反馈位于角落。按钮“时间图”切回原时间序列。
- 航迹视图直接打开，信号树管理每个子图独立的可用信号列表，新航迹默认经纬度模式；新选信号按选入顺序填充第一个空轴，顶部 ComboBox 可调整轴来源。SignalModel 的活动选择覆盖仅显示可用列表，不改变保留的时间图绑定；切换子图、配置、删除来源和会话恢复统一同步。
- 取消轴绑定保留可用信号；从树移除来源清空其轴映射；跨轴重复选择交换两轴。首次进入时已有时间图信号按列表顺序填充空轴；三轴满后仅添加候选，不替换绑定。不按名称猜测轴角色，无临时轴目标。左下方向坐标轴和旋转环共用原点，角落旋转环优先命中，窗口事件回归检查实际 QML 交互链。

### 多航迹与姿态数据流

SessionTrajectoryEntry 是每条航迹的值配置；SessionTrajectory 管理列表、活动项、共用候选和相机。继承的单项字段作为兼容现有 QML/controller 入口的活动编辑状态，entries() 在发布快照时覆盖活动项，select() 先提交编辑状态再切换。读写、重映射和清空均同步所有列表项，稳定字符串 id 不随信号行号改变。

TrajectoryItem::setSources 接收全部不可变 Store 来源。TrajectoryBuilder::buildFrame 独立验证每条路径，共用首有效地理原点，汇总可见有效路径的范围。TrajectoryFrame 保存路径与姿态原始段索引和仅含元数据的公共 bounds。原点一致时复用已准备路径；删除原点所属项必须用 localOrigin 校验再决定复用，防止沿用已删除航迹的旧原点。帧内重复坐标来源可共享同一缓存。

欧拉/四元数测量由 buildAttitude 校验各分量时间基并建立有效 runs；attitudeSample 在 runs 内调用 PlotSeriesStore 的原始最近样本查询，与位置独立。GUI 按共享游标计算读数和固定像素姿态几何，整体发布不可变 GeometryResult；SG 不查询原始 Store、不读 GUI 可变状态。观察相机、实际机体姿态分别变换，只有投影使用公共相机。

隐藏 PlotItem 接收全部可见航迹的位置和已启用姿态来源去重，支持原始时间游标步进；导出同样收集去重来源，不导出投影/模型派生值。时间图自身绑定保持独立。

## 游标编辑、航迹折叠与视图模板

游标精确输入由 QuickPlot 的示数双击打开编辑器，PlotItem::editCursorTime 在 m_dataMutex 下读取不可变 Store 快照、吸附原始时间并原子更新位置，现有 cursorChanged 继续驱动跨子图同步。T1/T2 分别编辑，ΔT 固定 T1 调整 T2，不增加保持间隔选项。

航迹配置折叠为 GUI 偏好，AppController 按子图保存运行期展开状态，TrajectoryPlot 根据状态调整画布顶部边距。它不改变信号绑定、Store 或会话 schema。

视图模板的独立 v1 JSON 封装复用 sessiondocument 的 v6 校验。AppController 共享 captureSession 收集配置，预览只匹配被配置引用的来源，applyViewTemplate 在全部映射、数值范围和 schema 校验通过后应用到当前 Store。样式整体替换不可变快照；不读取模板数据路径、不应用旧时间偏移。默认范围和游标由当前原始数据计算；延迟创建的子图通过缓存的 SessionPlot 获得完整配置。撤销保存此前配置及 Store 身份/代际，数据来源或样式变更后拒绝过期撤销。
