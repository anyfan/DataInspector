# Agent 与 CLI 自动化

DataInspector 提供 JSON CLI，可由 Codex、Claude、脚本或其他 agent 调用。CLI 控制一个正在运行的窗口，操作同步反映在界面中。本版提供 CLI，没有注册 MCP 服务或 Python SDK。

## 开启与连接

CLI 需要 Python 3.9+，仅使用标准库；软件本身不需要 Python。从仓库根目录运行（部署包中 exe 和 `datainspector_cli.py` 位于同一目录）：

```powershell
# 必须使用绝对路径；父目录存在、连接文件尚不存在。
$connection = Join-Path $env:LOCALAPPDATA 'DataInspector-agent.json'
Start-Process ./build_qt6-debug/bin/DataInspector.exe -ArgumentList @('--automation-file', ('"' + $connection + '"'))
# 等待连接文件出现后调用。
python ./tools/datainspector_cli.py --connection $connection help
python ./tools/datainspector_cli.py --connection $connection status
```

没有 `--automation-file` 时不监听端口。每个窗口使用独立连接文件；服务仅监听 IPv4 `127.0.0.1` 的随机端口，每次启动生成新令牌。连接文件允许持有人读写文件和修改工作区，应保存在当前用户的私人目录，不应提交到 Git 或作为数据文件分享。Windows 实际访问控制取决于父目录 ACL，Qt 文件权限不替代 Windows ACL。

正常退出会删除自己的连接文件。强制终止/崩溃可能留下旧文件；确认对应进程退出后删除再启动。已有文件不会被覆盖，开启失败时程序以状态码 2 退出。不支持连接到未开启自动化的旧窗口。

## Agent 调用流程

每次调用向 stdout 输出一行 UTF-8 JSON。成功为 `{"ok":true,"result":...}`，退出码 0；失败为 `{"ok":false,"error":{"code":"...","message":"..."}}`，退出码 1。CLI 参数语法错误输出到 stderr，退出码 2。`--timeout` 默认 60 秒，是单次调用总等待上限。

1. 调用 `help` 发现命令与参数约束。
2. 调用 `load --wait` 导入，检查任务实际成功。
3. 调用 `signals`，核对信号 ID、来源、样本数和 `generation`。
4. 使用该版本调用 `samples`、`statistics`、`nearest` 或信号设置命令。
5. 设置后检查 `status`／`signals`，需要保留时调用 `save_session`。

信号 ID 是当前工作区的编号，过滤信号树不改变编号。删除、加载、偏移、画笔变更、派生结果发布及会话恢复后，版本可能变化。所有使用信号编号的命令必须传入最近发现的 `generation`；收到 `stale_generation` 后重新列举并核对来源，不要自动将旧编号配上新版本重试。分析结果标注启动分析时的版本，用户可以在分析期间编辑界面。

所有修改命令及异步操作返回 `task`，含 ID、state、progress、时间、结果及错误。未带 `--wait` 时 `accepted:true` 只表示已接受；带 `--wait` 时 CLI 等到 succeeded/failed/cancelled，再以真实结果决定退出码。`wait --params-file` 可指定 `{"job":"任务ID"}` 恢复等待；无参数的旧 `wait` 仅等待空闲（包含航迹准备），不能证明任务成功。恢复会替换当前工作区，需要时先保存。

对修改或异步命令使用 `--request-id` 可安全重试：同一标识、相同命令和参数返回原任务；参数不同返回 idempotency_conflict。去重发生在 generation 检查之前，因此响应丢失后可以用原参数重试。不要用同一标识表达一次新的修改。标识仅在当前服务进程存续，最多保留 512 个任务；未带标识的已完成任务可被淘汰，带标识的任务保留至进程退出。达到容量且无法淘汰时明确拒绝新任务。只读同步查询不接受 requestId。

`jobs` / `job_status` 查询任务，`job_cancel` 请求协作取消。加载在读取块、MAT 变量之间检查取消，不会强行中断单次第三方解压调用；已完成的同步修改不支持撤销。取消派生计算保留定义，可用 rules_retry 重算。CLI 超时不取消服务端任务，输出保留已知 task；通过 ID 查询或原 requestId 重试。异步操作串行，另一操作返回 busy。

## 示例

为避免 PowerShell JSON 引号处理差异，推荐 `--params-file`，接受 UTF-8 或 UTF-8 BOM 文件。所有数据/会话路径必须是绝对路径，按软件所在机器解释。

```powershell
$cli = './tools/datainspector_cli.py'
$params = Join-Path $PWD 'build_qt6-debug/agent-params.json'
@{path='D:/data/flight.csv'} | ConvertTo-Json | Set-Content -Encoding utf8 $params
python $cli --connection $connection --timeout 180 load --params-file $params --request-id import-flight --wait

$listing = python $cli --connection $connection signals | ConvertFrom-Json
$listing.result.signals | Format-Table signal,name,sourceFile,samples
# 根据上表确认信号编号。
@{signal=0; generation=$listing.result.generation; start=100; end=200} |
    ConvertTo-Json | Set-Content -Encoding utf8 $params
python $cli --connection $connection statistics --params-file $params

@{rows=2; columns=1} | ConvertTo-Json | Set-Content -Encoding utf8 $params
python $cli --connection $connection layout --params-file $params
@{signal=0; generation=$listing.result.generation; plot=1; enabled=$true} |
    ConvertTo-Json | Set-Content -Encoding utf8 $params
python $cli --connection $connection plot_signal --params-file $params
```

也可传 `--params` JSON 字符串；不要同时使用两个参数入口。

## 命令与数据语义

| 命令 | 参数 | 作用 |
| --- | --- | --- |
| `help` | 无 | 每条命令的 inputSchema、outputSchema、taskSchema、taskResultSchema、mutating/asynchronous；类型、必填、枚举、范围及默认值 |
| `status` | 无 | 任务、版本、布局、绑定、会话状态；每图最多列出 1000 个绑定并标明 `truncated` |
| `wait` | 无 | CLI 轮询状态至空闲 |
| `signals` | `offset=0, limit=100` | 信号元数据分页，limit 1–1000；用 `nextOffset` 继续 |
| `objects` | 无 | 对象配置、稳定字段 ID、用途和来源绑定 |
| `clear` | 无 | 清空已加载数据、对象与绑定；不删除磁盘文件，布局保留 |
| `merge_signals` | `generation, groups:[{name,signals:[编号,...]}]` | 批量创建按时间合并规则；每批 1–256 组，每组 2–256 个不同原始信号；一次校验并提交，后台计算 |
| `object_create` | `name, type` | 创建 general 或 aircraft 对象，返回 object ID |
| `aircraft_configure` | `object, geographic, attitudeMode, radians, order, scalarLast, navigationToBody` | 配置飞机位置/姿态约定；姿态 0 关闭、1 欧拉、2 四元数，order 0/1 与界面两个顺序对应；其余为布尔值 |
| `object_bind_field` | `generation, object, field, signal` | 绑定已有字段，保留字段 ID、名称、用途；signal=-1 解绑 |
| `object_trajectory` | `object, plot` | 在指定子图显示飞机对象航迹；经纬度模式至少绑定 latitude、longitude |
| `trajectory_status` | `plot` | 航迹绑定及 attached/pending/ready/error、原点和姿态状态 |
| `trajectory_view` | `plot, preset` | 视角并自适应：0 等轴测、1 俯视、2 正视、3 侧视 |
| `capture_window` | `path` | 异步保存完整窗口内容为 PNG，含工具栏和信号树；之后 wait 并确认文件/状态 |
| `samples` | `generation, signal, offset=0, limit=1000` | 原始索引分页，limit 1–10000，返回 `[time,value]`；`nextOffset:null` 表示末页 |
| `statistics` | `generation, signal, start?, end?` | 闭区间原始样本 count、missing、min、max、mean、总体 stddev、rms；省略边界表示全范围 |
| `nearest` | `generation, signal, time` | 最近有效原始样本，等距时优先原始索引较小者 |
| `load` | `path` | 加载一个 CSV/TXT/MAT/XLSX 文件，重复文件拒绝 |
| `layout` | `rows, columns` | 行列各 1–8 |
| `plot_signal` | `generation, signal, plot, enabled` | 添加/移除时间子图绑定；plot 从 0 开始；重复添加幂等 |
| `style` | `generation, signal, color, width, style` | Qt 颜色字符串，线宽 1–20；线型 1 实线、2 虚线、3 点线、4 点划线、5 双点划线 |
| `rename` | `generation, signal, name` | 修改显示名称 |
| `offset` | `generation, signal, seconds` | 设置信号绝对时间偏移，单位秒 |
| `cursor_mode` | `mode` | 活动/最大化子图游标数量 0、1、2，沿用 GUI 同步行为 |
| `fit` | 无 | 所有子图自适应 |
| `save_session` | `path` | 原子保存，沿用现有覆盖行为 |
| `restore_session` | `path` | 异步恢复会话 |

时间包含信号偏移。统计不使用 LOD、不插值、不按采样间距加权。时间无效的样本不进入区间；区间内无效值计入 missing，其余有限值参与统计。空区间 count=0，数值统计为 null。分页保留无效值并编码为 null；最近样本无有效结果时返回 null。派生信号读取已经发布的计算结果。

任务查询、help/status、规则汇总等诊断入口可在后台工作期间使用；修改及数据分析有忙碌保护。单请求最多 64 KiB、最多 8 条连接，未提交完整请求的连接 10 秒后关闭。CLI 响应上限 4 MiB，大清单请分页。字段的约束以 help 返回的机器可读 schema 为准。

## 通过对象绘制飞机航迹

load → wait → signals，先依据数据说明、名称、原始值范围确认经纬度是度、高度是米，确认来源时间基兼容；不要按信号编号猜测。object_create 创建 aircraft；aircraft_configure 选择 geographic=true，若只画路径可设 attitudeMode=0，其余布尔约定显式传入。objects 发现 latitude/longitude/height 的稳定字段 ID，依次用 object_bind_field 绑定并 wait，然后 object_trajectory 指定子图。配置不会重建已有字段，绑定保留原名称与用途。

推荐 `aircraft_setup --wait`：传 generation、object（更新）或 name（创建）、config、bindings；bindings 直接用角色名映射信号编号，如 latitude/longitude/height/roll/pitch/yaw，也支持 XYZ 和四元数。config 包含 geographic、attitudeMode、radians、order、scalarLast、navigationToBody。配置和全部绑定在副本校验后一次提交，已有字段身份与名称保留；无效绑定不产生部分修改。可选 plot、cursorTime 同时展示航迹和启用游标。显示或游标失败会报告已提交的对象绑定，不回滚对象。

`aircraft_check {object}` 返回位置、姿态绑定是否齐全、必填与可选缺项、单位和坐标约定，以及各视图的准备状态、游标模式与 attitudeVisible。地理模式至少经纬度，高度可选；XYZ 至少两个轴；启用欧拉角需三个姿态角，四元数需四分量。姿态模式为 0 时检查明确标注关闭，不会把“有路径”当作“已有姿态”。只有游标启用且当前时刻有有效姿态时模型才可见。单位依据显式配置，不能代替对源数据说明的核对。

object_trajectory、trajectory_view 和带 plot 的 aircraft_setup/mapping_apply 任务会等待航迹准备，最长 30 秒；失败检查 trajectory_status。cursor_time 的 time 单位为秒、index 默认 1，沿用原始样本吸附。capture_window --wait 等待 PNG 写入完成；save_session 保留对象与绘图配置。

## 批量合并同源信号

signals 另提供 originalName。调用者应按物理来源、单位与原始名称确定分组，不能只因显示名称相似就合并；同一文件内分散各表、原始名称完全相同的信号可作为候选。明确核对分组后将 groups 数组提交给 merge_signals；服务不猜测同源关系，不按特定文件名适配。超出 64 KiB 请求上限时分批，每批完成 wait 并重新取得 generation。

控制器预先校验全部分组，自动按每处理配置 256 来源映射/规则的限制分配独立处理配置，批量发布输出占位后只调度一次计算。原始信号保留不变；每组形成一条派生输出，独立信号无需重复生成。该命令会创建新规则，超时后先检查 objects，不应盲目重发。

响应 accepted=true、completed=false 只表示定义已提交。wait 后检查 objects 中每条规则的 error 和输出；同一时间同值去重、缺失与有效重合保留有效值、同一时间不同有效值报错。冲突或 512 MiB 计算值预算不足时保留定义与来源，不将失败输出伪装为成功。详细语义见[对象与派生数据](objects-and-derived-data.md)。

## 合并预览与执行

推荐使用 merge_preview → merge_execute，避免重复创建规则。merge_preview 是只读后台检查，带 `--wait` 返回完整计划；merge_plan 以 id 查询。默认仅将同一物理文件内 originalName 相同的原始列归组，保留不同表来源；policy 可指定 sourceFile、tables、caseSensitive（默认 true）、aliases（原名到规范名的映射）。名称匹配是候选依据，单位与物理含义仍由调用者确认。

计划包含 generation、重复组、独立列数、已有规则、预计值内存、冲突及输出样本数；即使任务 succeeded，仍须检查 ready。ready=false 禁止执行。merge_execute 传计划 id，并建议携带 requestId、`--wait`；数据变化后计划失效。执行跳过已有相同原始来源集合的合并规则，同一计划重复执行复用结果，失败或取消后通过 rules_retry 重算现有定义。每实例最多保留 32 份计划，重启后失效。预览不修改数据，但会实际校验样本，耗时与数据量相关。

`rules` 提供 search、state=pending/succeeded/failed、offset、limit（默认 100），返回全局汇总及分页；`objects` 也支持 offset/limit，省略分页参数保留旧数组格式。`rule_update` 传 generation、object、rule、config，更新已有规则并保留输出身份；输出数量改变会拒绝。取消后可用 rules_retry 重新计算。GUI 处理页面提供搜索、状态筛选、20 条分页及结果摘要。

## 可复用飞机映射模板

template_save/template_read 保存或读取独立的 version=1 JSON；mapping_preview 列出候选、歧义及单位不兼容，mapping_apply 只接受完整、无歧义映射，参数另带 generation 和 object 或 name，可带 plot/cursorTime。

```json
{
  "version": 1,
  "config": {"geographic": true, "attitudeMode": 1, "radians": false,
             "order": 0, "scalarLast": false, "navigationToBody": false},
  "roles": {
    "latitude": {"aliases": ["lat", "latitude"], "unit": "deg"},
    "longitude": {"aliases": ["lon", "longitude"], "unit": "deg"},
    "height": {"aliases": ["height"], "unit": "m"},
    "roll": {"aliases": ["roll", "f_phi"], "unit": "deg"},
    "pitch": {"aliases": ["pitch", "f_theta"], "unit": "deg"},
    "yaw": {"aliases": ["yaw", "f_psi"], "unit": "deg"}
  }
}
```

角色可用 pattern 正则匹配，可额外约束 sourceFile/table；aliases 精确匹配原名。unit 是调用者的单位声明，不会自动换算数据。地理经纬度必须 deg、高度/XYZ 为 m；欧拉角随 radians 使用 rad/deg，四元数为 unitless。多个候选不会静默选第一个。模板只定义规则，不固化当前信号编号或特定文件名。

## 逻辑结果视图与导出

`signals {logical:true}` 返回成功合并输出和未被替代的信号；失败合并输出不进入逻辑集合，其原始输入继续保留。signal 编号保持原工作区编号。`logical_view {enabled:true}` 只调整显示；GUI 的“处理”栏通过“隐藏失败的合并结果”过滤失败输出，“信号”栏始终保留原始信号。不会删除数据。

`export_logical --wait` 参数 directory 为尚不存在的绝对目录，format=mat/xlsx。成功后得到 data.mat 或 data.xlsx 和 manifest.json；后者记录来源列、偏移、样本数、对象与完整规则定义及汇总。后台在临时目录写完数据和清单后一次发布；失败、取消、目录碰撞均不覆盖现有目标。这里导出的是逻辑结果数据，完整工作区重现仍使用 save_session。

本版未开放主题、窗口几何或任意 QObject 方法调用；help 是服务端可调用能力的完整清单。MCP 不在本次范围内。

`object_create` 支持 `type: "packetloss"`；`rule_update` 支持 `operation: "packetloss"` 及 `config.samplePeriod`（秒，正数为指定周期，0 保留兼容的一次估算入口）。新增派生规则窗口选择“丢包检测”后提供“自动识别”，为 GUI 显式操作，加载原始文件和恢复会话不触发识别或创建对象。派生输出仍可用既有 samples/statistics/rules 命令分析。

`rule_update.config` 的 packetloss 规则另支持 `lossMode: "time" | "sequence" | "stride" | "rate"`，以及数值 `counterStep`、`expectedStride`、`counterModulus`、`counterRate`、`counterTolerance`。默认依次为 time、1、1、0、1、0；修改时未指定参数保留原值。按时间模式要求输入为累计实际接收帧数，频率单位为应收帧/秒；发送序号不可替代。具体公式、分母和回绕限制见对象与派生数据手册的 Y 计数检测。

## 独立处理配置（会话 v15）

`object_create` 保留原命令名，type=general/aircraft 创建对象，type=processing/packetloss 创建独立处理配置。`objects` 为兼容客户端继续列出这两类定义，可按 type 区分；规则所属的 object 参数现指处理配置 ID。新规则仅允许放在处理配置中，不能放入常规或飞机对象；规则输入支持 `raw:<信号行号>`，也可使用配置内来源 ID 或此前输出 ID。删除/重绑对象字段不改变处理结果，旧会话恢复时会拆分原对象规则。
