# Agent CLI 接入 · 2026-10-15

需求：让 agent 调用 DataInspector 分析数据和设置软件。首版提供 JSON CLI，连接实际窗口，不增加 MCP SDK、Python API 或新绘图库。

实现：`--automation-file` 显式开启本机随机端口、生成令牌并原子发布连接文件；白名单命令覆盖发现、加载、原始分页/最近样本/闭区间统计、布局/绑定/样式/名称/偏移/游标模式、自适应和会话。Python3 标准库客户端支持 JSON 参数文件、等待空闲、结构化错误和退出码。设置复用 AppController，单线程工作池只读取不可变 Store 快照。generation 采用实例/恢复 epoch 与 Store counter，防止会话恢复时 counter 重复让旧 ID 被接受。部署脚本复制 CLI，Qt Network 随现有 Qt DLL 规则部署。

验证：Debug + MAT 应用和 automation_test 构建成功，qmllint 零 warning。真实 socket 测试覆盖认证、分帧/超限、参数拒绝不产生部分修改、原始统计/空区间/缺失值、分页/最近采样、设置与会话往返、旧版本拒绝。CLI 端到端测试启动实际应用（offscreen/software），覆盖中文路径、UTF-8 BOM 参数文件、发现/分析/设置/会话恢复与错误退出码。控制器、会话、启动参数回归同时执行。

最终 Debug 全量构建成功。定向 CTest：appcontroller_test、session_test、startupfiles_test、automation_test、automation_cli_test 共 5/5 通过；最后修改 CLI 超时处理与空启动参数校验后重跑两个自动化套件，2/2 通过。

## 实际 MAT 飞机航迹检查

用户要求使用 CLI 加载 `0911-1119-新飞管标称测试.DAT.mat`、配置飞机对象并绘制航迹。首版缺少对象/航迹命令，补充 objects、object_create、aircraft_configure、object_bind_field、object_trajectory、trajectory_status、trajectory_view 和 capture_window，均复用现有控制器。对象字段绑定保留稳定身份、名称及用途；没有根据该文件名或信号名在产品代码中添加特殊逻辑。

实际文件位于 test_file，大小 1,535,146,032 字节。通过真实 Python CLI load/wait/signals 成功加载 1 个文件、617 个信号，来源各表合计 2,655,876 行。依据 CLI 元数据与原始样本选择 p2 中 F_latDeg（76，度）、F_lonDeg（75，度）、f_hightAbs（77，米），每列 221,325 个样本、时间样本一致；原始统计包括零定位数据，没有修改或过滤源数据。创建“0911 新飞管标称测试飞机”，绑定经纬高，姿态关闭，避免推定未确认的姿态约定。

CLI 以 1×2 布局显示同一对象的三维等轴测与俯视图；两个 trajectory_status 均 ready=true、pending=false、error 为空。D3D11 日志确认使用 Intel Iris Xe 硬件适配器。CLI capture_window 保存并实际查看截图，save_session 保存对象/字段/布局/视角。运行产物保存在 `.local/agent-checks/0911-flight/`：trajectory.png、flight-trajectory.disession、validation.json、render.log；本机数据和运行证据不提交 Git。受执行环境隔离限制，实际验证驱动在同一执行环境启动应用与 CLI 子进程，所有软件操作仍由 CLI 完成；最初跨执行环境连接未成功，未将其作为成功证据。

扩展后的 automation_test、automation_cli_test（合成经纬高、对象字段身份、PNG、会话恢复和非法配置检查）2/2 通过；appcontroller_test、session_test、objectdata_test、trajectory_test 4/4 通过；Debug 受影响目标及 qmllint 构建通过。本次硬件证据仅针对该实际航迹截图和准备状态，不代表完整 GPU 回归或人工全功能验收，未验证 Release/部署包。

用户指出姿态未添加后，继续通过同一实际窗口的 CLI 为原飞机对象开启欧拉角，并绑定 p998 的 f_phi（507，滚转）、f_theta（508，俯仰）、f_psi（509，偏航）。三列均标注度、885,292 个有效样本；使用软件默认 FRD→NED、Rz·Ry·Rx 约定。原经纬高字段身份与绑定保留，启用单游标显示实测姿态。两图返回 attitudeMode=1、attitudeSources=[507,508,509,-1]、ready=true，error/attitudeStatus 均为空；实际检查 D3D11 截图中飞机模型可见。保留用户运行中调整的布局与已有曲线，更新原会话与 validation.json；新截图为 `.local/agent-checks/0911-flight/trajectory-attitude.png`。本次只改变运行配置，没有修改生产代码，也未重复运行代码测试。

## 测试任务 2：log_335.mat 全部同源合并

用户要求清除旧数据、加载 log_335.mat 并合并全部同源信号。先通过旧窗口 CLI 保存 before-task2.disession，然后更新并重启测试应用。新增 CLI clear 与 merge_signals，signals 增加 originalName；后者接收明确分组而非按文件名适配。AppController::mergeSignals 在局部副本中按每对象字段上限构建整批对象与 merge 规则，一次 validateObjects、提交占位和调度计算，避免逐字段触发后台任务。整批无效时不修改状态，异步冲突继续由现有规则引擎报告。

实际通过 CLI clear 确认 fileCount=0、signalCount=0，再加载 test_file/log_335.mat（308,464,208 字节），发现 p1–p5 共 1,592 个原始信号、130,160 行。按相同规范来源文件和完全相同的 originalName 分组，并额外核对大小写严格一致；保留名称中的 _2 等后缀。共 271 个重复组（256 组各 5 源、14 组各 2 源、1 组 3 源）、281 个独立信号。分 256+15 两批调用 merge_signals，形成 271 条派生输出；原始 1,592 列保留，总计 1,863 列，逻辑去重后为 552 个信号。所有规则 error 均为空。

通过真实 CLI 分页读取每张原表的时间轴，按组计算独立时间戳并集，271/271 条输出样本数一致；另完整读取五源“北向速度 北向速度”的所有原始样本，在 Python 独立去重排序并与 65,080 个输出点逐时间/值精确比较，通过。并集时间计数校验利用同一原表各列共享时间基的加载约定，不声称已经对全部输出的所有数值做逐点独立复算。

Debug 受影响目标及 qmllint 通过；automation_test、automation_cli_test、appcontroller_test、session_test、objectdata_test 共 5/5 通过。新增端到端覆盖 clear、无效批次拒绝无部分提交、交错时间合并、同时间不同值错误及会话恢复错误保留。运行配置和报告放在 `.local/agent-checks/log335/`，原始 MAT 文件未修改。未执行 Release、部署包或完整 GPU 回归。

初次测试因沙箱默认 TEMP 不可写失败；改用仓库构建目录下 test-temp 后通过，未修改生产逻辑来绕过限制。验证不包含人工 GUI、真实 GPU、Release 或部署包实机检查。加载/恢复响应表示接受，wait 表示空闲，后台失败仍需由状态与来源清单核对。详见[使用文档](../user-guide/automation-cli.md)。
