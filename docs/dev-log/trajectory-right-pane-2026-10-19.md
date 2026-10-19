# 航迹配置右侧栏 · 2026-10-19

## 行为

新增 TrajectoryPropertiesPane，作为 Main SplitView 右侧成员，默认宽 320、最小 260，支持拖动调宽和关闭；与信号属性侧栏互斥。图内保留配置、视角与适应，取消原顶部展开区、对象选择 Dialog 和样式 Dialog。配置、右键入口和错误提示均打开右栏；进入航迹模式仍不主动打开，也不强制绑定对象。

右栏统一管理活动航迹、增删、显隐、可选飞机对象、手动坐标来源及名称／颜色／线宽。坐标候选显式请求全体原始／派生信号；既有 trajectorySignalOptions 默认仍只返回当前航迹勾选来源，保留旧调用语义。无对象可直接设置经纬高或 XYZ，修改对象提供的坐标会解除该航迹的对象引用，不修改对象定义。样式与坐标点击应用后生效，选择航迹／对象及增删立即生效。

QuickPlot 通过显式 trajectoryEditor 引用共享右侧编辑器；不引入隐式上下文或新的弹出窗口。应用前捕获样式草稿，防止 configureTrajectory 发出刷新通知时覆盖待提交字段。配色使用独立 foreground，避免 Palette 自引用循环。格式不变，会话仍为 v21，模板仍为 v5。

## 验证

Debug DataInspector、session_test、trajectory_test 以及 Release DataInspector 构建通过，两者 qmllint 通过。最终 session_test/trajectory_test 2/2 通过（35.34 秒），包括源码与编译 QML 侧栏、主动对象选择、多航迹管理、样式、模式切换；Main 回归验证右侧位置、左树和图面保留、与信号属性互斥以及无需事先勾选即可直接配置坐标并保留名称。

Windows D3D11 mixedStateAndCurveLayout 通过（2.417 秒），已检查 trajectory-sidebar-gpu.png，确认右栏编辑与直接信号航迹。独立 QML 测试将侧栏接入同一 QQuickWindow，避免无宿主控件的有效可见性影响断言。未做用户真实数据手动验收。

Release 使用 bin-review 输出，保留用户正在运行的程序；新版副本为 bin/DataInspector-trajectory-pane.exe。

## 收敛为对象配置来源

用户要求航迹来源只通过对象配置，取消手动坐标编辑。右栏移除经纬高／XYZ 和坐标系控件、“飞机对象（可选）”改为“飞机对象”；应用仅提交样式，保留对象引用。进入航迹模式仍允许空白进入，但不再从原时间图的勾选信号推断坐标。普通信号选择、勾选、图例跨图移动及移除不会修改航迹来源，统一提示在飞机对象中配置。底层显式航迹配置接口及当前会话结构不变。

撤回本轮新增的全体坐标候选参数，保留既有 trajectorySignalOptions 接口。更新回归验证已有曲线勾选不推断坐标、对象来源建立与会话恢复、勾选／拖动不改对象坐标、样式应用保留对象引用；原内核来源隔离及多航迹导出测试改用显式配置准备输入。

最终 Debug/Release 构建与两者 qmllint 通过，appcontroller_test/session_test/trajectory_test 3/3 通过（40.13 秒）。Windows D3D11 mixedStateAndCurveLayout 通过（2.333 秒），已检查 trajectory-object-pane-gpu.png。未做用户真实数据手动验收。新版副本：bin/DataInspector-object-trajectory.exe；默认 Release bin 配置已恢复，原运行窗口保留。

## 样式统一与处理页整理

对象列表的航迹样式入口改为同一右栏，打开时不创建或修改航迹；颜色使用与信号属性共用的 ColorEditor，名称、颜色、线宽和显隐在应用后提交。处理列表按卡片集中呈现名称、状态、操作、来源和参数，预览放入悬停提示，单页时隐藏翻页控件。修复移除旧样式弹窗时误删的信号栏宽度约束。

时间合并允许 1–32 个输出，每组输入独立保存、计算和诊断。统一规则输入并集供依赖调度使用，复制映射组内引用；会话升至严格 v22，不提供迁移。

验证：Debug/Release 构建与 qmllint 零 warning。appcontroller、session、objectdata、trajectory、automation、automation_cli 回归均通过（objectdata 最终重跑通过）。原生 Windows D3D11 的 multipleOutputEditorQml（1.328 秒）与 mixedStateAndCurveLayout（2.102 秒）通过，检查 processing-cards-gpu.png 与 unified-trajectory-style-gpu.png；未做用户真实数据人工验收。Release 试用副本为 build_qt6-release/bin/DataInspector-processing-outputs.exe。

## 输出卡片直接绑定输入

按用户反馈删除顶部共用合并输入区。每张合并输出卡片直接提供信号树选择、拖入添加、拖入已有行替换和移除，拖拽命中携带输出索引，避免修改上一组。当前信号树绑定的输出以边框提示。处理结果列表去掉绘图复选框，仍可在左侧处理信号树选择绘图。

本次验证：Debug/Release 构建及 qmllint 通过；appcontroller_test、objectdata_test 共 2 套通过。后者新增跨输出卡片拖拽命中与输入隔离覆盖。原生 Windows D3D11 的 multipleOutputEditorQml 通过，已检查 merge-output-cards-gpu.png 和 processing-no-checkbox-gpu.png；试用副本 build_qt6-release/bin/DataInspector-merge-inline.exe。

## v0.5.0 发布整理

2026-10-19：项目版本更新至 0.5.0，合并重复发布说明并明确会话 v22／模板 v5 的格式边界。Debug 与 Release 完整构建、两套 qmllint 零 warning；Debug 全量 CTest 14/14（83.90 秒），Release 全量 CTest 14/14（94.68 秒）通过。Release 使用独立 bin-review 输出，正式便携包存放 dist/v0.5.0，按现有 GitHub v* 标签流程发布。全量测试为 Fusion/software/offscreen；本轮已有局部原生 D3D11 截图验证，不代表全量 GPU 或真实数据人工验收。
