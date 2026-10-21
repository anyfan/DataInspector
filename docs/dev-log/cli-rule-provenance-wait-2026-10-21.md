# 规则来源、计算终态和 Python 启动入口

本轮反馈指出 inputIds 的内部 UUID 与 raw:编号 不同、--wait 结果含混，以及 Python 启动警告。保留原 inputIds，增补直接输入映射与传递原始依赖，提供 rules(sourceSignal,generation) 查询。API 修订升至 3，协议仍为 1。

确认旧 derive 任务完成时未更新 result.completed，且规则状态此前由全局 deriving 推断。现以发布的逐规则计算记录/诊断判断终态，完成结果给出 completed、evaluationState、generation 和规则输出；缓存有效的其他规则保持成功状态。会话恢复先调度计算再发完成信号，避免恢复任务过早结束。

本机 PATH 中 Python 3.14 启动器及实际解释器在 -I/-c 空测试中均可独立复现路径警告，早于 CLI 脚本；已安装 Python 3.11 可安静启动。新增独立 PowerShell 入口，探测候选或接受显式解释器，实际 CLI stderr 不过滤，系统安装和 PATH 不变。构建与部署同步分发两份脚本。

验证覆盖用工作池门控阻塞求值后任务保持 running、发布后才成功并返回可用样本、按原始来源追溯下游规则、含派生的会话恢复 --wait，以及默认 PowerShell 入口无启动警告且保留真实调用错误。

Debug 构建成功，qmllint 零警告，全量 CTest 14/14 通过（225.20 秒，offscreen/software）；多输出合并的下游来源按实际引用分支追溯，缓存规则不随其他任务变成 pending。构建目录 PowerShell 入口与源码 SHA256 一致，显式解释器调用的真实失败仍返回 JSON 与非零退出码。未验证 Release 或真实 GPU 显示。
