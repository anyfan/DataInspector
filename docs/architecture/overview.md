# 整体架构

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联代码**：`src/quick/`、`qml/`

DataInspector 使用 Qt 6.8 Qt Quick Scene Graph。本文描述当前职责与数据流；交互细节见用户指南，线程约束见[并发](concurrency.md)，历史方案只在问题溯源时查阅。

## 模块划分

| 源码目录 | 职责与主要文件 |
| --- | --- |
| `qml/` | Main、SignalBrowser、SessionDialogs、ObjectManager、QuickPlot、TrajectoryPlot 等界面组件 |
| `src/quick/` | `main.cpp` 入口、`startupfiles.*` 参数解析、`qmltypes.h` 显式 QML_FOREIGN 注册 |
| `src/quick/controller/` | AppController：生命周期、加载、子图、航迹、会话、模板、对象、计算、图片及关于信息 |
| `src/quick/automation/` | 本机 JSON CLI；复用控制器与不可变数据快照，见[自动化架构](automation.md) |
| `src/quick/io/` | DataLoadWorker/DataExportWorker；CSV/TXT/MAT/XLSX 读写与共用表结构、导出校验 |
| `src/quick/models/` | SignalModel、signalmetadata、objectdefinition（对象定义及规则依赖/计算） |
| `src/quick/state/` | SessionDocument、ViewConfiguration、ViewTemplateDocument 和 JSON 校验 |
| `src/quick/items/` | PlotItem/TrajectoryItem：视图交互、游标、后台预览与 Scene Graph 节点提交 |
| `src/quick/render/` | Store → 范围索引 → LOD → Geometry，轴工具与航迹构建，无 QQuickItem 依赖 |

详细文件入口与 include 约定见[源码导航](../../src/quick/README.md)。目录整理不改变共用构建库或线程边界。

## 数据与渲染

1. `AppController` 在工作线程创建 `DataLoadWorker`，串行解析文件并建立分块极值索引；结果以 `LoadedTable` 值类型投递到 GUI 线程。
2. `PlotSeriesStore` 持有不可变原始序列及索引；模型、子图绑定和对象来源使用运行期信号行号。
3. `PlotItem` 将视窗请求交给 `PlotLodScheduler`（最多两个后台线程）；`PlotGeometryBuilder` 把 LOD 转成裁剪后的屏幕空间三角带，`updatePaintNode` 提交 SG 节点。
4. 游标、统计和导出访问 Store 原始快照，不从 LOD 取值，不插值。导出在 `DataExportWorker` 后台完成，PNG 经 SG 抓取后交给导出线程编码。

画笔、偏移和计算结果通过替换不可变快照发布。`PlotItem` 视图/游标状态由 `m_dataMutex` 保护；SG 节点由渲染线程拥有。细节见[渲染管线](render-pipeline.md)、[数据与导出](data-and-export.md)、[并发](concurrency.md)。

## 对象与独立处理

`DataObject` / `ObjectRule` 是共用值表示。general/aircraft 是对象，只管理字段及飞机配置；processing 是独立处理配置，持有原始来源映射与有序规则。GUI 统一展示处理配置，丢包检测作为规则操作。`dataObjects` 提供两类配置的总表，`objectConfigurations` / `processingConfigurations` 按类型直接构造分表，不先生成再丢弃另一类配置。规则来源名称用 ID 哈希查找；处理摘要不携带全量原始候选，`processingRuleInputs(id, beforeRule)` 只为当前编辑配置返回原始输入、失效来源占位及此前规则输出。编辑器关闭时不生成候选。

处理输入仅引用原始信号或本配置此前输出。后台任务捕获配置和原始 Store 快照，按拓扑顺序计算，通过 `publishComputed` 发布结果。失效键包含数值参数、原始数组身份与偏移；只重算受影响规则及下游，独立分支结果/错误保留。取消后未发布分支合并到下一任务，512 MiB 值预算包含保留结果。对象字段重绑、复制及删除不改变独立处理配置。

移除文件先计算依赖闭包，再一次删除原始/派生行；`remapSignalReferences` 统一更新对象字段、规则输出、全部航迹位置/姿态来源与候选，最后刷新视图。手动解绑保留配置，显式移除文件会清理依赖规则。运行期身份仍使用行号，新增持久引用时必须经过统一重映射。

`merge` 在单次任务内通过 `MergeTimelineCache` 复用共享原始时间基；精确时间去重，有效值覆盖缺失值，同时间不同有效值使输出失败。生产求值器仅使用时间轴缓存；独立直接排序参考实现位于 `tests/support/merge_reference.h`，只供数值回归和基准使用，不进入应用或生产 API。

`packetloss` 单输入，time 模式读取时间戳，sequence/stride/rate 模式读取原始 Y 计数；模糊回绕、复位和乱序保守拒绝。规则统一为单项输出 count/cumulative/window，默认 window；双输出定义拒绝读取或保存。窗口率双指针扫描为 O(n) 时间、O(1) 额外空间，单输出只分配一个数组。自动识别在工作池内按文件和完整偏移时间轴分组，GUI 提交检查代际、取消与数量上限；导入和恢复不自动识别。周期预填最多采样 8192 个间隔，完整扫描在后台。语义见[对象与派生数据](../user-guide/objects-and-derived-data.md)。

## 界面与交互

`Main.qml` 协调子图、对象/处理编辑页、文件拖放及内部拖拽。`SignalBrowser` 用来源分组索引进行搜索/展开，150 ms 合并搜索；`signalBrowserSection` 与逻辑结果筛选共同决定可见来源，定位信号可切换栏目。模型重置、Esc、失焦和布局变化取消拖拽。

`ObjectManager` 分别实例化为对象字段编辑器与处理编辑器。规则 Pane 与列表互斥，离开页面保留草稿，取消/切换配置结束编辑；来源变动使旧草稿失效。输入槽共享允许列表和 ID/行号索引，树点击、勾选、拖拽只改草稿，应用后才调用控制器增改规则。

`QuickPlot` 负责轴、图例及游标编辑。游标只吸附原始样本；T1/T2 精确编辑分别定位，ΔT 固定 T1 调整 T2。成对步进按第一条游标相邻原始样本时间差同步移动，保持间隔；控制器同步各子图。交互规则见[视图操作](../user-guide/view-operations.md)。

## 航迹

`SessionTrajectory` 的 tracks 列表是唯一状态来源，active 只选择列表项；不维护活动副本。`appcontroller_trajectory.cpp` 将飞机对象字段解析为位置及姿态来源，统一管理相机、候选与时间联动。UI 来源编辑通过对象页面完成；直接来源 API 仍服务视图模板及内核测试。

`TrajectoryBuilder` 校验每条路径时间基，构建有效样本段、归一化位置和空间简化索引；地理路径共用首有效原点，删除原点来源后重新校验缓存。最多两个轨迹工作线程生成不可变预览，GUI 发布相机/姿态几何，SG 仅消费结果。姿态与位置分别查询原始样本，观察相机不替代机体测量姿态。详见[航迹指南](../user-guide/trajectory.md)。

航迹子图保留隐藏 PlotItem 接收去重的位置/姿态来源，支持原始时间游标步进；此时暂停曲线 LOD 并取消在途任务。时间图绑定独立保留，切回后恢复 LOD。导出收集原始来源，不导出投影缓存。

## 会话、模板与自动化

会话只读写当前 v21；模板只读写独立 v5，不保留逐代迁移。会话分别保存 objects 与 processing，模板只保存被引用来源提示、样式和视图；应用模板完成全部映射后才提交，可在 Store 身份/代际未变化时撤销一次。保存/恢复失败保留已有文件和当前状态；恢复先暂存数据，全部成功后切换。格式见[会话](../user-guide/session.md)。

会话修改状态由控制器集中追踪；关闭/替换由 QML 提供保存、放弃、取消。关于页读取构建时生成的元数据和嵌入更新记录，不在启动时查询 Git。CLI 复用控制器与不可变快照，协议、任务和合并计划见[自动化架构](automation.md)。

## 已知边界

- 离屏测试不代表 GPU 视觉、性能或真实数据验收。
- 锁外渲染快照与多级冷 LOD 摘要试验已撤回；再做同方向前须读[退化证据与决定](../dev-log/render-lock-cold-lod-2026-10-09.md)。
- 会话 v21、模板 v5 不迁移旧格式；丢包仅是 processing 内的规则操作，CLI 不再接受 packetloss 配置类型。

信号枚举由控制器按来源行号持有，随统一来源删除重映射；会话和模板按信号保存，应用模板及撤销恢复定义。状态行只引用来源与显隐，刷新时捕获共享枚举并注入不可变状态任务，不在每个视图重复保存定义。`SignalPropertiesPane` 作为 Main 横向 SplitView 的右侧成员编辑属性、枚举表格和状态行管理，SignalBrowser 通过显式 propertiesEditor 引用协调入口，返回与切换保留草稿，来源结构变化清除草稿。处理 UI 统一“位提取”入口，统一通过输出列表逐行定义位段，单 bit 使用位数 1，无提取方式选择。

属性面板在打开时跟随信号名称／勾选框的直接点击切换，拖拽、处理来源选择及状态来源选择保持各自优先级。未应用草稿按信号缓存，仅实际更改的草稿保留，避免外部重命名或批量时间偏移后被无修改的旧快照覆盖。名称、样式、枚举及原始信号时间偏移通过 configureSignalProperties 统一校验后提交；时间轴溢出或无效枚举在修改前拒绝。派生时间仍随输入变化。文件／分组偏移在同一右侧编辑器内设置，保留混合值提示；原重命名和偏移 Dialog 已删除。

位提取和公式规则允许 1–32 条输出，ObjectOutput 保存每行的起始位、位数、符号或表达式；规则公共标量参数仍作为 API 缺省值。输出列表编辑按 ID 保留既有系列，新输出追加；删除输出前检查下游引用，全部验证成功后统一发布和重映射。后台扫描自动字宽，按有效值及所需位范围取最小 8/16/32；默认小端，大端先按有效字宽交换字节。每个公式单独编译并生成阶梯标记，输入时间基仍严格相同。所有输出一起计入既有 512 MiB 值预算与取消检查，缓存键包含每行定义、端序及自动字宽设置。
