# 状态与计算债务清理（剩余三项，2026-10-10）

分支：codex/state-debt-cleanup。接续[第一批](state-debt-cleanup-2026-10-10.md)，完成当时剩余的规则级计算、共用视图/独立模板和职责拆分。

## 改动

- 规则记录保存计算参数和不可变输入数组身份，拓扑传播失效；字段重绑、偏移及参数变更只清空受影响规则及下游。同对象独立分支继续显示原结果与错误，名称/画笔不重算数值。未提交和预算失败的规则不安装完成记录；下一次调度合并重算。512 MiB 值预算包含全部保留输出。
- ViewConfiguration 提取布局、子图、航迹和坐标范围的共用结构与校验；会话 v10 和 v1–v10 读取兼容保持。ViewTemplateDocument 独立保存 v2，仅包含被引用信号的文件名提示、表/原始名、显示样式和视图。来源完整路径、时间偏移、对象定义和绝对游标时间不写模板。严格读取旧模板 v1 并转换，再次保存为 v2；旧程序不能读取 v2。
- SignalBrowser.qml 接收显式控制器、对象编辑器和主题参数，负责搜索/导航及树手势；Main.qml 保留跨图命中与窗口协调。SessionDialogs.qml 负责会话/模板选择、重定位、匹配及保存提醒。Main.qml 从 1731 行收敛到 1046 行，实际交互通过既有事件回归检查。
- appcontroller_objectevaluation.cpp 负责规则任务取消/捕获/接收；appcontroller_objects.cpp 保留定义操作。appcontroller_templates.cpp 负责模板匹配、应用与撤销；appcontroller_session.cpp 保留完整会话恢复。CMake 共用列表和现行文档同步。

## 验证

Debug 完整构建通过；DataInspector_qmllint 零 warning。Fusion/offscreen 完整 CTest 12/12 通过，80.67 秒。新增回归覆盖同对象独立分支、下游传播、子规则编辑、取消不安装状态、名称/画笔不重算；模板覆盖精简 v2、旧 v1 迁移、无关来源剔除、未知字段/重复或缺失引用拒绝、失败保留原对象和原子保存。已有源码和编译 QML 的实际鼠标事件、信号树拖拽/搜索、关闭提示、对象编辑、会话迁移及模板撤销继续通过。

Qt 生成的 qmlcache_loader.cpp 编译时仍有 SDK Q_GLOBAL_STATIC 的 variadic macro 警告；qmllint 没有 QML warning。本批未修改 SDK 生成代码。

未运行 Release、真实硬件 GPU 光栅/帧率对比、人工 GUI 或干净环境部署验收；上述自动化证据不代表总体交互提速。

## 范围

这三项完成。第一批中提到的稳定系列 ID 属于另一个格式/Store/绑定迁移方向，本批仍使用集中维护的运行期行号，并保留旧会话和普通 XY 的直接轴入口。未恢复已撤回的锁外快照/冷 LOD 试验；未新增已遗弃功能。
