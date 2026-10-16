# 自动化 CLI 架构

`tools/datainspector_cli.py` 使用 Python 标准库，通过一次连接一个请求的换行 JSON 协议访问 `automation/automationserver.*`。这不是 HTTP 或 MCP。服务在 Main 窗口创建后按 `--automation-file` 显式开启，绑定本机随机端口，以 QSaveFile 发布带令牌的连接描述文件。

服务仅允许显式命令，校验参数类型、范围和未知字段，不使用 QObject 反射执行外部方法。设置在 GUI 线程调用现有 AppController，会话格式不变。startupfiles 跳过自动化参数值，防止连接 JSON 被误识别为会话。

对象/飞机命令调用现有 createDataObject、configureDataObjectAircraft、bindObjectField 和 showObjectTrajectory；先查询稳定字段 ID，绑定保留名称与用途，不按文件名或信号名特殊适配。trajectoryRenderState 只在 GUI 线程读取已附着 TrajectoryItem 的准备状态、错误、原点，不读取 SG 节点。setTrajectoryView 复用现有视角/自适应。captureAutomationWindow 将窗口 contentItem 交给原有 exportPlotImage，沿用等待可见航迹准备、Qt grabToImage、后台 PNG 原子写入链。

clear 复用 AppController::clear。merge_signals 接收明确的来源组，AppController::mergeSignals 在局部副本中构建所有对象/字段/merge 规则，统一 validateObjects 后提交，避免逐字段 API 反复启动/取消计算。超限或无效分组在提交前返回；计算继续走已有规则依赖与后台预算链。对象按字段上限分配，不按信号名或文件名特判。originalName 由 SignalModel 提供，外部调用者负责判定同源和单位。

AppController::automationSnapshot 只在 GUI 线程捕获 Store 不可变序列。单线程 QThreadPool 接收快照，执行完整原始统计、分页及最近采样查询，不访问控制器、QML、PlotItem 或可变 Store。通过排队调用回到服务线程应答。后台分析与界面编辑可并行，结果携带捕获时版本；服务析构取消统计扫描并等待工作池，只删除令牌仍属于自身的描述文件。

外部 generation 由服务实例/会话恢复的 UUID epoch 与 Store generation 组合。恢复安装新 Store，其 counter 可与旧 Store 重复，因此不能仅暴露 counter。版本不匹配时拒绝，防止旧 ID 指向另一来源；调用者仍需核对新列举的来源。

协议 v1 请求为 `{"token":"...","command":"status","params":{}}` 加换行，响应为 `{"ok":true,"result":...}` 或 `{"ok":false,"error":{"code":"...","message":"..."}}` 加换行，随后断开。限制见[用户文档](../user-guide/automation-cli.md)。令牌不出现在 CLI stdout，连接文件及存放目录是本机信任边界。服务无远程监听、无自动发现。

automation_test 覆盖真实 socket、原始样本/统计、设置/恢复、认证、版本失效、参数及帧大小。可选 Python 的 automation_cli_test 启动实际应用，在 offscreen/software 后端调用真实 CLI，不连接用户现有窗口。

## 任务、配置与检查计划

commandcatalog.* 是命令发现及参数校验的共同来源；automationtasks.cpp 负责任务生命周期、进度、取消、请求指纹与去重，automationcommands.cpp 承载批量配置、模板、合并计划和逻辑导出入口。请求可附 requestId，SHA256 指纹由命令与规范 JSON 参数生成。去重缓存仅内存保存，最多 512 项；只淘汰未带幂等键的终态任务，无法淘汰时拒绝新增。任务 ID 不等同工作区版本，不能跨服务启动复用。

任务成功来自加载/导出完成信号、实际规则错误检查或航迹准备状态，不从“空闲”推断。规则有有效输出但带诊断时列为 warning；无输出且有错误为 failed。渲染及飞机配置任务最长等待 30 秒。取消为原子标志协作，不强杀线程、不回滚已提交的同步定义；派生取消保留可重试定义。后台期间允许任务、规则等诊断查询。

appcontroller_automation.cpp 的 setupAircraft 在局部对象副本中完成类型、预置字段、配置和所有来源校验，保留已有字段身份，一次发布并调度。显示航迹及游标在提交后进行，失败会如实返回而不伪装事务回滚。aircraftCheck 分别报告位置、姿态、声明单位、约定、游标及模型可见状态。映射模板是独立 version=1 配置，支持别名/正则及文件/表筛选，多个候选或单位声明不兼容都拒绝应用；不猜测单位或改写原样本。

merge_preview 在 GUI 线程捕获原始快照与分组策略，在现有串行工作池运行真实 merge 校验，回传时间并集规模、冲突、已有来源集合规则及版本化计划。预览结果 ready=false 可以是一次成功完成的检查，但禁止执行。每实例最多 32 计划。merge_execute 拒绝过期计划，跳过已存在规则，一次提交所有新组；计划保存执行任务及结果，重复执行不新增定义。

## 共享合并时间线

models/mergetimeline.* 在每次 evaluateObjects 内建立有界 64 MiB 缓存。键包含不可变时间数组（或 points 数组）身份、样本数、偏移及存储形式；快照保持数组生存期，按值读取不触发 detach。同组时间基只排序一次，缓存时间并集、来源索引及同时间段边界；后续列仍分别读取原始值、处理缺失和冲突，保持原算法精确时间语义。结果可共享只读时间数组，值和范围索引独立。不同偏移/数组不误用；超预算仍计算但不驻留。取消扫描及 512 MiB 值预算保持有效。

merge_benchmark 以同一快照分别选择旧排序路径和共享路径，哈希全部输出时间和值。它是测量工具，不以绝对耗时作为 CTest 断言。此优化不涉及 Plot LOD、渲染锁或已撤回的冷 LOD 试验。

逻辑视图通过 SignalModel 可逆来源掩码隐藏被成功 merge 替代的输入，不改 Store 行号。逻辑导出收集相同集合及来源清单，DataExportWorker 在同父目录临时文件夹写完全部文件后 rename 发布；目录存在则失败。会话仍 v11，任务、计划和显示掩码不写入会话。
