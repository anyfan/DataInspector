# 历史债务清理记录（2026-10-18）

> 本次工作的历史记录；当前规则见根目录 AGENTS.md、架构总览和会话指南。

## 已处理

- 单人使用，不以旧版兼容为默认要求。会话仅接受 v17，视图模板仅接受 v2；删除旧版参数默认、单航迹补建、飞机字段推断、对象规则拆分等迁移分支。
- 删除 detachObjectProcessing、AppController::migrateObjectProcessing 及其 QML 初始化调用。保存操作不再隐式生成新处理配置/ID，非法对象内规则直接拒绝。
- viewConfigurationFromJson 去掉版本/旧颜色参数，复用已校验的活动航迹字段，删除重复解析与为比较而完整序列化航迹的临时 JSON。
- MergeTimelineCache::get 的 reuse 参数从未传 false，移除无调用方分支；保留真正使用的缓存命中和 64 MiB 上限。
- 核心文档从逐次追加的版本说明改为当前状态：修正模板 v1/v2、会话 v10/v17、丢包单/双输出、对象与处理分离等矛盾。测试页以套件职责和运行方式为主。
- 根导航明确历史/记忆不能替代当前需求；.rgignore 默认排除 docs/dev-log，历史文件仍受 Git 管理，可以按链接或 --no-ignore 显式读取。
- 原迁移测试改为当前格式往返、全部旧会话版本拒绝、缺字段拒绝、失败保留内存状态/磁盘文件。保留窗口率、来源删除、QML、CLI 等功能回归。

## 保留项与后续边界

| 项目 | 当前用途与处理依据 |
| --- | --- |
| v17 双输出丢包规则 | 当前格式可以保存，涉及输出 ID、图例、下游依赖、复制及编辑；删除应单独定义格式与引用策略。本次没有静默丢弃已有结果。 |
| packetloss 配置类型 | GUI 已统一处理，但 CLI/schema/状态仍接受此类型。统一为 processing 需要联动处理类型校验及现有文件，不能只移除标签。 |
| trajectory 顶层 axes/geographic | 与活动 track 冗余，但仍是当前会话/模板字段且校验一致；删除需同步格式版本、严格模板键和测试。 |
| evaluateObjects 的直接排序路径 | tests/objectdata_test.cpp 与 tools/merge_benchmark.cpp 用于对比共享时间轴的数值和成本；不是生产回退。后续可移到测试/基准专用实现。 |
| 无索引 Store 查询路径 | 手工构造快照/内核测试仍会走此路径；属于数据表示处理，不能按 legacy 注释当作死代码删除。 |
| 处理配置的 QVariant 输入列表 | processingConfigurations 为每配置构造候选，原始候选可能随追加输出发生分离复制；大信号量×多配置值得专项测量，再考虑独立输入模型或按活动配置查询。 |

未恢复已撤回的锁外渲染快照或冷 LOD 摘要；本次不改变 SG/游标/线程边界，不宣称性能提升或全仓库最优。

## 验证

- Debug 应用与五个核心测试目标构建成功，DataInspector_qmllint 零 warning。
- appcontroller_test、session_test、objectdata_test、automation_test、trajectory_test：最终全部通过（5/5，62.14 秒）。
- 实际 DataInspector.exe + tests/automation_cli_test.py 集成通过，覆盖发现、分析、设置、会话恢复、UTF-8 及错误处理。CTest 缓存 Python 3.14 启动时报 operation not permitted；用 PATH 中可运行的 Python 直接运行同一脚本通过，没有修改测试逻辑或降低断言。
- git diff --check、变更 Markdown 本地链接及默认历史检索隔离检查通过。
- 未做 Release、真实 GPU、HiDPI、真实数据性能或部署验收。
