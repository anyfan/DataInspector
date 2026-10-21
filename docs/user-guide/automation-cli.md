# Agent 与 CLI 自动化

DataInspector 提供 JSON CLI，可由 Codex、Claude、脚本或其他 agent 调用。CLI 控制一个正在运行的窗口，操作同步反映在界面中。本版提供 CLI，没有注册 MCP 服务或 Python SDK。

## 开启与连接

CLI 需要 Python 3.9+，仅使用标准库；软件本身不需要 Python。已经打开的窗口可通过右上角“设置 → CLI 自动化…”点击“开启 CLI”，不需要重新启动或重新加载数据。对话框显示当前窗口的连接文件，可复制连接路径或 PowerShell 调用命令，将示例末尾的 status 换成 help、signals 等命令。关闭对话框不关闭服务；点击“关闭 CLI”或退出窗口才关闭监听并清理连接文件。重新开启生成新的令牌，旧连接信息失效。开关不写入会话，下次普通启动默认关闭。

部署包中 exe、`datainspector_cli.py` 和 `datainspector_cli.ps1` 位于同一目录。也可以继续使用启动参数，从仓库根目录运行：

```powershell
# 必须使用绝对路径；父目录存在、连接文件尚不存在。
$connection = Join-Path $env:LOCALAPPDATA 'DataInspector-agent.json'
Start-Process ./build_qt6-debug/bin/DataInspector.exe -ArgumentList @('--automation-file', ('"' + $connection + '"'))
# 等待连接文件出现后调用。
python ./tools/datainspector_cli.py --connection $connection help
python ./tools/datainspector_cli.py --connection $connection status
```

没有 `--automation-file` 且未从界面开启时不监听端口。每个窗口使用独立连接文件；服务仅监听 IPv4 `127.0.0.1` 的随机端口，每次启动生成新令牌。连接文件允许持有人读写文件和修改工作区，应保存在当前用户的私人目录，不应提交到 Git 或作为数据文件分享。Windows 实际访问控制取决于父目录 ACL，Qt 文件权限不替代 Windows ACL。

正常退出会删除自己的连接文件。强制终止/崩溃可能留下旧文件；确认对应进程退出后删除再启动。已有文件不会被覆盖，开启失败时程序以状态码 2 退出。普通方式打开的窗口可从设置中开启自动化后连接。

## Agent 调用流程

每次调用向 stdout 输出一行 UTF-8 JSON。成功为 `{"ok":true,"result":...}`，退出码 0；失败为 `{"ok":false,"error":{"code":"...","message":"..."}}`，退出码 1。CLI 参数语法错误输出到 stderr，退出码 2。`--timeout` 默认 60 秒，是单次调用总等待上限。

1. 调用 `help` 发现命令与参数约束。
2. 调用 `load --wait` 导入，检查任务实际成功。
3. 调用 `signals`，核对信号 ID、来源、样本数和 `generation`。
4. 使用该版本调用 `samples`、`statistics`、`nearest` 或信号设置命令。
5. 设置后检查 `status`／`signals`，需要保留时调用 `save_session`。

### 版本和能力检查

每次连接新窗口都先读 help。线协议 `protocolVersion=1` 与命令 API 修订 `apiVersion=3` 分开：协议相同不代表命令集相同，磁盘程序更新也不会改变已运行进程的命令集。`capabilities` 返回版本、commands、catalogHash（完整命令描述及 schema 的 SHA256）、maxRequestBytes、maxEnumBatchSignals；`status.result.cli`、`help` 响应顶层 `cli` 和连接文件的 `cli` 提供同样信息。help.result 仍是原命令数组，已有脚本可继续使用。旧服务没有这些字段时不能推断它支持新命令，应以该连接的 help 为准。

需要固定调用契约时，CLI 可传 `--expect-api-version 3` 和/或 `--expect-catalog-hash <已核对的SHA256>`。这时先向服务查询 capabilities，检查通过才发送目标命令；不匹配或旧服务无法提供版本时返回 `version_mismatch`，不发送目标修改。线协议不匹配返回 `protocol_mismatch`。检查包含在 --timeout 总时限内；未指定检查选项时不额外发起预检请求。命令集新增/变更时维护 API 修订并重新核对 schema 指纹，不要自动把意外出现的新指纹当成已审核的契约。

```powershell
$help = python $cli --connection $connection help --ascii | ConvertFrom-Json
$help.cli | Select-Object protocolVersion,apiVersion,catalogHash
$help.result.command
# 核对 schema 后，在后续修改中锁定本次发现的契约。
python $cli --connection $connection status --ascii --expect-api-version 3 --expect-catalog-hash $help.cli.catalogHash
```

### Windows 中文与管道

CLI 默认输出 UTF-8 JSON。若 Python 输出的字节被 PowerShell 按其他代码页解码，会出现终端乱码，不能据此判断枚举存储已损坏。脚本推荐添加 `--ascii`：所有非 ASCII 字符写成 JSON 的 `\uXXXX` 转义，ConvertFrom-Json / JSON 解析器会恢复原字符；请求参数和实际保存文本不变，成功与错误响应都适用此选项。

```powershell
$listing = python $cli --connection $connection signals --ascii | ConvertFrom-Json
$listing.result.signals | Format-Table signal,name,originalName
```

需要直接显示 UTF-8 JSON 时，可在当前 PowerShell 会话设置 `$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)`，再使用 `python -X utf8 ...`。参数文件仍使用 UTF-8 或 UTF-8 BOM；不使用 ANSI 编码写中文参数。

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
| `capabilities` | 无 | 线协议/API 版本、命令集 SHA256、命令名称及请求/枚举批量上限 |
| `status` | 无 | 任务、版本、布局、绑定、会话状态；每图最多列出 1000 个绑定并标明 `truncated` |
| `wait` | 无 | CLI 轮询状态至空闲 |
| `signals` | `offset=0, limit=100` | 信号元数据分页，limit 1–1000；用 `nextOffset` 继续 |
| `objects` | 无 | 对象配置、稳定字段 ID、用途和来源绑定 |
| `clear` | 无 | 清空已加载数据、对象与绑定；不删除磁盘文件，布局保留 |
| `merge_signals` | `generation, groups:[{name,signals:[编号,...]}]` | 批量创建按时间合并规则；每批 1–256 组，每组 2–256 个不同原始信号；一次校验并提交，后台计算 |
| `object_create` | `name, type` | 创建 general/aircraft 对象或 processing 处理分组，返回 object ID |
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
| `signal_enums` | `generation, signal` | 读取信号共享枚举，返回 generation、signal、enums |
| `set_signal_enums` | `generation, signal, enums` | 整表替换共享枚举，空数组清空；同时刷新引用该信号的状态视图 |
| `signal_enums_batch` | `generation,signals` | 一次读取 1–256 个不同信号的名称及共享枚举 |
| `set_signal_enums_batch` | `generation,updates:[{signal,enums},...]` | 全批校验后原子替换，返回实际应用的规范枚举及名称 |
| `signal_properties` | `generation, signal, name?, color?, width?, style?, timeOffset?, enums?` | 与信号属性面板共用原子校验；省略字段保留当前值，派生信号不能设置 timeOffset |
| `offset` | `generation, signal, seconds` | 设置信号绝对时间偏移，单位秒 |
| `cursor_mode` | `mode` | 活动/最大化子图游标数量 0、1、2，沿用 GUI 同步行为 |
| `fit` | 无 | 所有子图自适应 |
| `save_session` | `path` | 原子保存，沿用现有覆盖行为 |
| `restore_session` | `path` | 异步恢复会话 |

时间包含信号偏移。统计不使用 LOD、不插值、不按采样间距加权。时间无效的样本不进入区间；区间内无效值计入 missing，其余有限值参与统计。空区间 count=0，数值统计为 null。分页保留无效值并编码为 null；最近样本无有效结果时返回 null。派生信号读取已经发布的计算结果。

任务查询、help/status、规则汇总等诊断入口可在后台工作期间使用；修改及数据分析有忙碌保护。单请求最多 64 KiB、最多 8 条连接，未提交完整请求的连接 10 秒后关闭。CLI 响应上限 4 MiB，大清单请分页。字段的约束以 help 返回的机器可读 schema 为准。

## 编辑信号枚举

先用 signals 核对编号和 generation，再用 signal_enums 读取。枚举最多 256 项，每项严格包含 value（唯一安全整数，范围 ±9007199254740991）、text（非空且最多 256 字符，不含换行或 NUL）、color（Qt 有效颜色）。set_signal_enums 整表替换，更新一项时先读取并修改数组再提交；enums=[] 清空。枚举与界面共享，save_session 保存后可恢复。signals 分页不携带枚举表，避免元数据响应过大。

```powershell
$listing = python $cli --connection $connection signals | ConvertFrom-Json
@{signal=0; generation=$listing.result.generation; enums=@(
    @{value=0; text='关闭'; color='#687887'},
    @{value=1; text='开启'; color='#278451'}
)} | ConvertTo-Json -Depth 6 | Set-Content -Encoding utf8 $params
python $cli --connection $connection set_signal_enums --params-file $params --wait
```

signal_properties 可在一次调用中提交名称、样式、原始信号时间偏移和枚举，所有字段先验证再修改；无效枚举或偏移不会留下部分修改。两个修改命令均支持 --wait、--request-id。数据或样式修改后重新获取 generation；旧版本不会自动重试。

### 一次批量应用和核对

`set_signal_enums_batch {generation,updates:[{signal,enums},...]}` 一次处理 1–256 个不同信号；每项整表替换，enums=[] 清空。服务先校验全批的编号、重复项、枚举值、文字和颜色，再一次提交；任意项无效或 generation 过期时全批不改。状态视图只统一刷新一次。

成功结果包含 generation、updated（本次接受的信号数，含内容未变化的项）、signals（每项为 signal/name/originalName/enums）。返回的是实际应用的规范定义，颜色规范化为 #AARRGGBB，无需再逐项读取；不会生成逐样本的“实际值枚举”。需要另行核对时，`signal_enums_batch {generation,signals:[编号,...]}` 一次读取 1–256 个不同信号及枚举。

批量写入沿用任务和 --request-id 幂等机制；响应丢失后用同一 requestId 和原参数重试。单请求仍限 UTF-8 编码后 64 KiB（即使用 --params-file 也一样）；超限需分批，每一批独立原子提交，不承诺跨批事务。枚举修改不改变原始数组，generation 是编号/数据版本，不是枚举编辑的比较交换版本。

```powershell
# 更新记录中的 signal 必须来自刚核对过的 signals，不能按名字猜编号。
@{generation=$listing.result.generation; updates=@(
    @{signal=0; enums=@(@{value=0; text='关闭'; color='#687887'}, @{value=1; text='开启'; color='#278451'})},
    @{signal=1; enums=@(@{value=0; text='待机'; color='#687887'}, @{value=1; text='运行'; color='#278451'})}
)} | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 $params
$applied = python $cli --connection $connection set_signal_enums_batch --params-file $params --ascii --request-id enums-flight-001 --wait | ConvertFrom-Json
if (-not $applied.ok) { throw $applied.error.message }
$applied.result.signals | Format-Table signal,name,originalName,enums
```

### 状态字：先定义位段，再给输出批量加枚举

按协议能整值解释、互斥的分类状态可以直接配置原始信号枚举；多个 bit 可同时有效的故障字、标志字应先拆成独立输出。多 bit 的分类字段按其位段提取后再配置枚举。不要因名字含“状态”就给整个组合数值贴单一标签；GNSS 等状态字的位号、位宽、端序和定义以实际协议为准，CLI 不猜测。

下面是说明流程的虚构位段，不代表任何飞控或 GNSS 协议。bit 从 0 开始；startBit=0/bitCount=1 提取最低一位，startBit=1/bitCount=2 提取位 1–2。autoWordBits 确定有效字宽，byteOrder 表达实际输入字节顺序，signedField 选择位段有无符号。

```powershell
# 已通过 signals 核对 $wordRow 是原始状态字编号，并通过 help 核对命令。
@{name='状态字处理'; type='processing'} | ConvertTo-Json | Set-Content -Encoding utf8 $params
$created = python $cli --connection $connection object_create --params-file $params --ascii | ConvertFrom-Json
if (-not $created.ok) { throw $created.error.message }
$group = $created.result.object
$listing = python $cli --connection $connection signals --ascii | ConvertFrom-Json
@{generation=$listing.result.generation; object=$group; config=@{
    name='状态字拆位'; operation='bitfield'; inputs=@("raw:$wordRow"); autoWordBits=$true; byteOrder='little'
    outputs=@(
        @{name='标志位'; startBit=0; bitCount=1; signedField=$false},
        @{name='分类位段'; startBit=1; bitCount=2; signedField=$false}
    )
}} | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 $params
$made = python $cli --connection $connection rule_create --params-file $params --ascii --wait | ConvertFrom-Json
if (-not $made.ok) { throw $made.error.message }
$ruleId = $made.result.outputs[0].rule
$objects = python $cli --connection $connection objects --ascii | ConvertFrom-Json
$rule = ($objects.result | Where-Object id -EQ $group).rules | Where-Object id -EQ $ruleId
$outputs = @($rule.outputs) # 保留服务端返回的顺序和 series，不能推算新编号。
$listing = python $cli --connection $connection signals --ascii | ConvertFrom-Json
@{generation=$listing.result.generation; updates=@(
    @{signal=$outputs[0].series; enums=@(@{value=0; text='未置位'; color='#687887'}, @{value=1; text='置位'; color='#d95319'})},
    @{signal=$outputs[1].series; enums=@(@{value=0; text='待机'; color='#687887'}, @{value=1; text='运行'; color='#278451'}, @{value=2; text='维护'; color='#0072bd'}, @{value=3; text='保留'; color='#a2142f'})}
)} | ConvertTo-Json -Depth 10 | Set-Content -Encoding utf8 $params
python $cli --connection $connection set_signal_enums_batch --params-file $params --ascii --wait
```

可分别用 state_lane 将各输出加到状态视图的独立行。位段规则创建和枚举应用是两个明确步骤：规则创建的 --wait 先确认数值计算成功，枚举批量更新再确认语义；后一步失败不自动删除已创建的规则。不要在重试时重复创建规则。最后用 save_session 或 view_template_save 保存规则、位段定义及输出枚举。

## 常用编辑命令覆盖

命令复用界面控制器和校验。`rename` 修改信号显示名，`rename_reset {generation,signal}` 恢复原名；`style` 修改曲线样式，`trajectory_style` 修改航迹样式，两者分别作用于信号和航迹。

| 命令 | 参数 | 行为 |
| --- | --- | --- |
| `object_create` | `name,type` | type=processing 创建处理分组；general/aircraft 创建对象 |
| `object_rename` | `object,name` | 重命名对象或处理分组 |
| `object_duplicate` | `generation,object` | 复制对象或处理分组，返回新 object ID |
| `object_remove` | `generation,object` | 删除对象或分组及所属输出，保留原始数据 |
| `object_field_add` | `object` | 添加未绑定标量字段，返回 field ID |
| `object_field_rename` | `object,field,name` | 重命名字段 |
| `object_field_remove` | `object,field` | 删除可选字段；飞机必需字段及依赖保护沿用界面 |
| `rule_inputs` | `generation,object,beforeRule?` | 查询处理组允许的原始输入与此前规则输出 |
| `rule_create` | `generation,object,config` | 创建处理规则；config 与 rule_update 共用严格 schema；--wait 检查实际计算结果 |
| `rule_remove` | `generation,object,rule` | 删除规则；仍被下游引用则拒绝 |
| `trajectory_style` | `plot,track,name?,color?,width?,visible?` | 更新指定航迹，省略字段保留；width 1–12，不改变当前活动航迹 |
| `trajectory_select` | `plot,track` | 选择活动航迹 |
| `trajectory_remove` | `plot,track` | 移除航迹，保留来源对象 |
| `state_status` | `plot` | 查询状态模式及行列表 |
| `state_lane` | `generation,plot,lane,signal,name,visible` | lane=-1 追加，其他值替换指定行，并切到状态视图 |
| `state_lane_remove` | `plot,lane` | 删除状态行 |
| `state_lane_move` | `plot,lane,direction` | direction=-1 上移、1 下移；越界拒绝 |
| `view_mode` | `plot,mode` | mode=curve/trajectory/state；保留原绑定，不自动猜测航迹来源 |
| `plot_axes` | `plot` | 查询曲线坐标范围、normalizeY、blendOverlaps |
| `plot_axes_set` | `plot,xMinimum?,xMaximum?,yMinimum?,yMaximum?,normalizeY?,blendOverlaps?` | 合并当前值后统一校验提交，沿用 X 联动和视图撤销 |
| `plot_clear` | `plot` | 清空该子图曲线绑定，保留数据；状态行和航迹使用各自移除命令 |
| `scope_offset` | `generation,signal,scope,seconds,add?,group?` | scope=signal/table/file；从原始信号定位目标，默认设置绝对偏移，add=true 累加；可显式指定界面分组键 group |
| `file_remove` | `generation,path` | 按 signals.sourceFile 的绝对路径卸载文件和依赖输出，不删除磁盘文件 |

`track` 是 trajectory_status.tracks 中的稳定 id，不是活动项或数组下标；状态行 lane 则是当前行号，移除或重排后重新查询。航迹样式只确认配置提交，渲染准备仍查询 trajectory_status。对象/规则增删复制后可能重新计算，需等待空闲再获取 generation；对象复制和删除任务确认的是定义变更，计算结果用 rules 检查。规则创建失败可能保留已提交定义，检查任务错误，修正后 rule_update 或 rules_retry，不要盲目重复创建。

典型处理流程为 `object_create {type:"processing",name:...}` → `rule_inputs` → `rule_create --wait` → `rules`/`signals`。规则输入可用 `raw:<编号>` 或组内来源/输出 ID。如需保留航迹颜色、处理组及信号名称，最后调用 save_session。

## 完整视图模板

`view_template_save {path}` 导出 `.diview` v6，包含全部对象、处理组和规则、视图、信号样式及枚举；不含原始数据和计算数组。`view_template_preview {path}` 返回原始来源 rows/options、plotCount、objectCount、processingCount、ruleCount、outputCount。v5 在内存中读取迁移，原文件保持不变，另存才升级。

`view_template_apply {generation,path,mapping,fixedRanges?}` 需要将预览每个 row.id 映射到核对后的原始信号编号，例如 `mapping:{"0":1,"1":0}`；派生输出无需匹配。应用保留已有配置并分配新 ID，重新计算导入规则；使用 --wait 检查这些规则的实际结果。失败可能保留已经提交的配置，检查任务错误后修正规则，避免重复导入。无派生规则时任务直接完成。`view_template_undo {}` 一次恢复导入前配置并删除新输出；之后的来源/样式/偏移或对象规则编辑可使撤销失效。导入和撤销切换 Store 时轮换 generation 的 epoch，旧编号请求需重新发现。上述修改命令支持 requestId。

这四个命令是工作区视图模板，与 `template_save/template_read/mapping_*` 的飞机字段映射模板分开。完整格式及范围见[会话与视图模板](session.md)。

CLI 尚未覆盖全部界面操作：缺失会话文件替换、普通范围 MAT/XLSX 导出和单子图 PNG 导出、GUI 自动丢包识别、主题和窗口几何仍无对应命令；已有 export_logical 和 capture_window 分别用于逻辑数据与完整窗口。help 是当前可调用能力的完整清单。

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

`rules` 提供 search、state=pending/succeeded/failed、offset、limit（默认 100），返回全局汇总及分页；`objects` 也支持 offset/limit，省略分页参数保留旧数组格式。`rule_update` 传 generation、object、rule、config，更新已有规则并保留输出身份；位提取、公式和时间合并通过 outputs 编辑输出数量，仍被下游引用的输出不能删除。取消后可用 rules_retry 重新计算。GUI 处理页面提供搜索、状态筛选、20 条分页及结果摘要。

### 按原始信号查规则与等待计算

API 修订 3 增加明确的来源信息。`inputIds` 保留原表示（objects/rules 中可能为内部字段或输出 UUID），不能假定等于创建时的 `raw:<编号>`。每条规则另返回：

- `inputSources`：按输入顺序列出 id、kind=raw/output、signal（当前来源或上游输出的编号）；原始绑定另有 rawId="raw:编号"，未绑定时 signal=-1/rawId 为空。
- `rawInputSignals`：直接原始输入的编号，按输入顺序；引用上游结果的输入不列入这里。
- `sourceSignals`：沿本组此前输出展开后的原始来源编号集合，去重并排序，适合按原始信号匹配整条处理链。

`rules {generation,sourceSignal:编号}` 按原始信号查询相关规则，包含通过上游输出间接引用它的规则；仍可配合 search/state/offset/limit。sourceSignal 必须是当前原始信号，需传最近发现的 generation。响应含 generation；summary 对来源范围汇总，search/state 继续只过滤分页行。来源查询不依赖 UUID 或显示名称。

`rule_create --wait` 等待目标规则已发布的计算结果，成功结果明确 `completed:true,evaluationState:"succeeded"`，另带 generation、rules、checkedRules、errors、warnings；rules 中直接包含输入来源和输出 series，可用于下一次批量枚举，无需按原编号反查 UUID。未等待时 `accepted:true,completed:false,evaluationState:"pending"` 表示定义已受理。计算失败时任务 failed，检查 errors；取消不会伪装为完成。rules 最多回传 256 条，超出时 rulesTruncated=true，完整清单用 rules 分页查询。

规则的 pending/succeeded/failed 依据各自已发布的计算记录和诊断，其他规则正在计算不会把已有结果统一改成 pending。rule_update、rules_retry、合并执行、含处理的模板应用，以及含处理的会话恢复也沿用计算终态确认。空样本结果可以是成功，不能仅凭样本数或程序空闲推断终态。

```powershell
@{generation=$listing.result.generation; sourceSignal=$wordRow} |
    ConvertTo-Json | Set-Content -Encoding utf8 $params
$related = python $cli --connection $connection rules --params-file $params --ascii | ConvertFrom-Json
$related.result.rules | Select-Object id,inputIds,rawInputSignals,sourceSignals,outputs,state
```

### Windows 启动警告与 PowerShell 入口

本机复现的 `Failed to find real location of ...python.exe` 来自 PATH 中 Python 3.14 的启动阶段：即便只运行 `python -I -c ...`、尚未加载 CLI 脚本也会打印；它不能由脚本的 --ascii 或 stdout 编码修复。这是一次本机诊断，不代表所有 Python 3.14 安装都会出现。不要将 CLI 成功响应前的该提示误作服务端错误。

随程序提供 `datainspector_cli.ps1`。它先探测 PATH 的 python.exe，再查找当前用户常见 Python/uv 安装，选择 Python 3.9+ 且启动无 stderr 的解释器；探测失败的候选不会用于发起 CLI 请求。可用 `-Python <绝对路径>` 或环境变量 DATAINSPECTOR_PYTHON 指定解释器；显式指定的解释器启动异常会返回 python_startup_failed，不静默切换到另一环境。此入口不安装 Python、不修改 PATH 或系统设置。仅探测输出被捕获，实际 CLI 的 stdout、stderr 和退出码全部保留。

```powershell
& './tools/datainspector_cli.ps1' --connection $connection status --ascii
# 使用已验证可正常启动的解释器；路径按本机安装调整。
& './tools/datainspector_cli.ps1' -Python 'C:/Python311/python.exe' --connection $connection help --ascii
```

设置窗口复制的 PowerShell 命令使用 exe 旁的 ps1 入口；Python 脚本仍可由明确的解释器路径直接运行。每次新连接仍先检查 help/capabilities，当前 API 修订为 3、线协议保持 1。

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

`object_create` 使用 `type: "processing"` 创建处理配置；`rule_update` 支持 `operation: "packetloss"` 及 `config.samplePeriod`（秒，正数为指定周期，0 请求初次自动估算）。新增派生规则窗口选择“丢包检测”后提供“自动识别”，为 GUI 显式操作，加载原始文件和恢复会话不触发识别或创建对象。派生输出仍可用既有 samples/statistics/rules 命令分析。

`rule_update.config` 的 packetloss 规则另支持 `lossMode: "time" | "sequence" | "stride" | "rate"`，以及数值 `counterStep`、`expectedStride`、`counterModulus`、`counterRate`、`counterTolerance`。默认依次为 time、1、1、0、1、0；修改时未指定参数保留原值。按时间模式要求输入为累计实际接收帧数，频率单位为应收帧/秒；发送序号不可替代。具体公式、分母和回绕限制见对象与派生数据手册的 Y 计数检测。

## 独立处理配置

`object_create` 保留原命令名，type=general/aircraft 创建对象，type=processing 创建独立处理配置。`objects` 列出这两类定义，可按 type 区分；规则所属的 object 参数现指处理配置 ID。新规则仅允许放在处理配置中，不能放入常规或飞机对象；规则输入支持 `raw:<信号行号>`，也可使用配置内来源 ID 或此前输出 ID。删除/重绑对象字段不改变处理结果。会话只接受当前 v22，不迁移旧对象规则。

丢包规则的 `rule_update.config` 另支持 `rateMode: "count" | "cumulative" | "window"` 及 `rateWindowSeconds`（有限正数秒，默认 1）。新规则默认 window，仅生成所选的一条输出；count 为累计帧数，另两项为对应范围的百分比。模式及时长写入 v22 会话；仅允许单输出规则，修改指标保留输出身份和样式。旧 packetloss 配置类型及双输出定义拒绝提交。

rule_update.config 支持 autoWordBits、byteOrder，以及位提取／公式的 outputs 数组（1–32 项）。每项包含既有输出 id（新输出省略或空字符串）、name、startBit、bitCount、signedField、expression；按运算类型使用各字段。保留 id 维护原系列、画笔与引用；删掉仍被下游引用的输出会拒绝整个更新。bitfield 每行定义独立位段，expression 每行定义独立公式，共用 inputs。

时间合并（merge）的 outputs 支持 1–32 项；每项 inputs 独立指定 2–256 个输入 ID，config.inputs 为全部组的去重并集。各组独立合并，错误仅影响对应输出；保留输出 id 可维护已有样式和引用。
