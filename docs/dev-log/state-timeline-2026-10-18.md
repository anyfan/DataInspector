# 状态时间轴第一版（2026-10-18）

实现二值与多值枚举的状态子图，来源支持整个数值、单 bit 和位段，映射名称及颜色可编辑，状态行可隐藏、排序和删除。保留原始时间游标与范围联动，支持前后变化定位、精确区间悬停、未知／未定义值区分和亚像素变化提示。瞬时事件、事件表及累计统计留在后续范围。

新增不可变精确区间索引与后台屏幕预览，Scene Graph 顶点颜色矩形绘制。状态配置整合来源移除重映射、清空、文件偏移、PNG 等待和导出来源收集。会话升级 v19，模板升级 v4；按项目约定只接受当前格式，不增加迁移。

语义与操作见 [状态时间轴](../user-guide/state-timeline.md)。验证：Debug 和 Release 的 DataInspector 构建及各自 qmllint 均通过（零 QML warning）；Debug 的 session_test、appcontroller_test、objectdata_test、trajectory_test、automation_test、startupdrop_test 六套回归全部通过（42.33 秒）。另在 Windows D3D11 / Intel Iris Xe 硬件后端运行 stateTimelineQmlConfiguration，通过状态色带像素颜色、变化跳转、十行滚动及鼠标选行检查，截图位于 build_qt6-debug/state-timeline-gpu.png。该单场景检查不代表完整 GPU 视觉回归、真实数据性能或人工交互验收。

混合视图修正：取消状态行名区固定 150 像素留白，改为按文字宽度自适应并限制上限；空图不撑宽普通曲线的共享轴留白。GridLayout 不再强制所有行等高，独占一行的状态图在与曲线混合时按可见状态行数收紧（最多 8 行且不超过均分高度），曲线占用剩余空间；并排子图、最大化与全图 PNG 平铺保留各自布局语义。新增 mixedStateAndCurveLayout 回归。

混合布局验证：Debug／Release 构建及两套 qmllint 均通过；session_test 与 appcontroller_test 全部通过（11.81 秒）。mixedStateAndCurveLayout 在 Windows D3D11 硬件后端通过空图、名称宽度、动态行数、时间／游标同步、并排／最大化／恢复及截图检查；截图 build_qt6-debug/mixed-state-layout-gpu.png。

用户修正布局要求：上下排列恢复网格等高，删除按状态行数压缩子图的逻辑。状态行名基础留白固定为 48 逻辑像素，不随名称长度变化；长名称省略且悬停显示完整名称。混合布局回归改为验证短名／长名共享边距不变、行数变化不改变子图高度。

等高与长名称修正验证：Debug／Release 构建及各自 qmllint 通过；session_test、appcontroller_test 全部通过（11.33 秒）。D3D11 下 mixedStateAndCurveLayout 通过，长名称不改变共享边距，实际绘图区场景 X 起点和宽度一致，截图已更新。

状态图轴线／悬停修正：移除状态图左侧 Y 轴线，保留其他边框及共享时间轴几何；普通数值图边框不变。悬停查询为空时隐藏 ToolTip，避免滑动或停在状态行下方空白时出现无文字窄方框。新增真实鼠标在状态行与空白区移动的回归。

轴线／悬停验证：Debug／Release 构建与 qmllint 通过，session_test 和 appcontroller_test 通过（13.53 秒）；真实鼠标分别移到状态行与下方空白区，验证有效提示显示、空白提示隐藏，D3D11 场景检查通过。

状态名称布局调整：取消左侧窄名称栏和整块选中底色，名称移到对应色带上方，在绘图区宽度内显示。每条状态使用 20 像素名称区＋32 像素色带区，QML 和后台几何共享布局常量；相应更新悬停、滚动、行选择与 GPU 像素检查。状态图不再贡献额外左侧共享留白，维持网格等高与 X 轴对齐。

名称新布局验证：Debug／Release 构建及 qmllint 均通过，session_test 和 appcontroller_test 全部通过（11.97 秒）。D3D11 的状态文字／色带像素、悬停空白隐藏和十行滚动选择检查通过，state-timeline-gpu.png 已更新并检查名称显示在色带上方。

按用户示意改为左侧曲线图标＋顶部图例：每条色带恢复 32 像素，移除独立名称行，顶部相同颜色的曲线图标与标题自动换行，点击图例选择并定位状态行。左侧只预留固定 28 像素图标区域，保留 X 轴对齐和网格等高。

图标／图例布局验证：Debug／Release 构建和 qmllint 均通过，session_test 与 appcontroller_test 通过（12.04 秒）；D3D11 检查通过，验证 32 像素行高、左侧图标选行、顶部图例点击选行并滚动定位、状态色带像素与空白提示隐藏。state-timeline-gpu.png 已更新。

拖动闪烁修正：原先每次 request 都清除帧并重启定时器，连续移动造成空白和准备提示。改为保留当前来源的帧并按时间／宽度重投影，色块与文字同步；后台任务限频合并但不因视窗变化中断，完成后继续最新视窗，来源变化仍取消并清空。准备提示只在首次无帧时显示。新增连续范围变更、大小变化、来源失效及 D3D11 拖动过程像素回归。

拖动修正验证：Debug／Release 与两套 qmllint 通过，session_test 和 appcontroller_test 全部通过（12.34 秒）。新增回归验证连续视窗变化时帧和文字保持、投影位置正确、尺寸变化及来源失效；D3D11 连续六次平移期间逐帧抓取检查色带颜色且准备提示不显示，全部通过。

## 状态行活动游标数值

- 按用户确认，左侧曲线图标旁显示各行在当前活动时间游标处的数值；双游标跟随选中的 T1/T2，整字显示原值，位段显示解码值。未知或无游标显示“—”，长数值省略并在提示中显示全文及游标编号。
- 左侧固定 76 逻辑像素并继续使用共享轴留白，名称和读数变化不改变宽度；保持每行 32 像素和上下子图等高。
- 通过 PlotSeriesStore 最近原始样本查询，并与区间构建复用 StateTimeline::decode，不依赖 LOD 或后台像素聚合帧。
- 验证：Debug/Release DataInspector 和两者 qmllint 通过；session_test 全套通过（4.50 秒），覆盖最近样本与前值保持的区别、位段解析、活动游标切换和混合布局对齐。Windows D3D11 状态图测试通过（7.68 秒），截图 state-timeline-readout-gpu.png 已检查，包含读数和拖动保留帧检查。未做用户真实大数据手动验收。

## 信号搜索与状态图左侧间距

- 修复文件名或分组路径包含搜索字时，下级信号全部通过筛选的问题。现在只按信号名称忽略大小写筛选，并保留命中信号的祖先路径；仅文件名/分组名命中不会展示无关信号。
- 状态图左侧固定留白由 76 缩至 60 逻辑像素，读数靠近图标左对齐；保留共享轴留白、行高和 X 轴对齐。
- Debug 构建及 qmllint 通过；appcontroller_test/session_test 全套 2/2 通过（11.80 秒），新增文件名 .DAT 与搜索 AT 冲突、分组名命中、大小写和清空恢复回归测试。Windows D3D11 状态图测试通过（7.44 秒），已检查 state-timeline-spacing-gpu.png。
- 原 Release exe 被用户打开的进程占用，未关闭该窗口；通过既有 DATAINSPECTOR_BIN_DIR 参数在 bin-review 完成 Release 构建和 qmllint，再恢复默认 bin 配置。新 exe 复制到 bin/DataInspector-preview.exe，可复用原部署依赖运行；原 DataInspector.exe 仍为正在使用的版本。


## 2026-10-19：切换入口与状态接缝

切换状态时间轴的菜单动作仅改变视图模式，不发出状态行配置请求；需要时点击“状态行…”再打开右栏。普通接缝的深色竖条来自原先的像素内变化判断：单次边界也生成紫色密集标记。现要求同一屏幕桶内至少两次切换才标记，单次变化按相邻状态色衔接。

Debug/Release DataInspector 和两者 qmllint 通过；session_test 全套通过（6.70 秒）。Windows D3D11 的 stateTimelineQmlConfiguration 和 mixedStateAndCurveLayout 通过（12.288 秒），新增菜单切换不打开属性栏断言及非整数像素边界仅包含相邻两种状态色的像素断言，已查看 state-joins-gpu.png。原 Release 程序被占用，未关闭用户窗口，改用 bin-review 构建后恢复默认 bin 配置；新副本为 bin/DataInspector-state-joins.exe。未做用户真实数据的手动验收。


## 2026-10-19：统一视图切换

新增共用 ViewModeMenu，曲线／状态图和航迹图右键均以一级“切换视图”提供曲线视图、航迹视图、状态时间图，并标记当前模式。删除状态顶部“曲线视图”和航迹顶部“时间图”按钮。航迹入口直接调用 enterTrajectoryMode，允许无对象进入；对象选择仍可从图内配置打开。返回曲线同时关闭航迹与状态模式，保留原绑定。

Debug/Release DataInspector 与两者 qmllint 通过；session_test、trajectory_test 2/2 通过（26.70 秒）。新增断言覆盖三项子菜单、状态与曲线互切、航迹直接进入不打开对象选择、无对象身份、后续主动打开对象配置，以及航迹→状态→曲线切换；航迹源码及编译 QML 均通过。测试切回曲线时显式使用当前图的 plotContextMenu，避免误触 Loader 尚待销毁的旧航迹菜单。未做用户真实数据手动验收。Release 在 bin-review 输出后恢复默认 bin 配置，可运行副本为 bin/DataInspector-view-menu.exe。
