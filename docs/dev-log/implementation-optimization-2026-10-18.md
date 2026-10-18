# 五项实现优化记录（2026-10-18）

> 本次工作的历史记录。当前行为以架构总览、会话指南与源码为准。承接此前债务清理，并非尚待执行的计划。

## 最终实现

1. 对象/处理列表在序列化前按类型筛选，复用 configurationToVariant；不再构造完整 dataObjects 后丢弃另一类。规则来源标签通过 ID 哈希查找，替代输入×候选的嵌套扫描。
2. processingConfigurations 只返回配置摘要，不携带全量原始候选；编辑器打开后，通过 processingRuleInputs(id, beforeRule) 生成一个配置的原始候选、失效字段占位及此前规则输出。无效配置/规则返回空，关闭编辑不生成候选。每次原始来源扫描先建立输出行集合，避免逐原始行扫描全部规则输出。CLI dataObjects 的字段 ID 表示仍保持明确，GUI 摘要转换为 raw:行号。
3. 丢包统一为 processing 配置内的 packetloss 算子；移除 packetloss 配置类型、CLI 类型枚举、单/双输出分支及复制双输出入口。count/cumulative/window 仍可编辑切换，但每条规则只有一个稳定输出；需要多个指标时创建多条规则。
4. 航迹 axes/geographic 只保存到 tracks，顶层仅保存 active 索引。会话升级 v18，独立视图模板升级 v3，只接受当前版本，不迁移 v17/v2 或更早文件；原文件不会被改写。旧镜像字段、旧类型及双输出定义明确拒绝。
5. 生产 evaluateObjects 删除参考模式开关与旧合并分支。独立点排序实现位于 tests/support/merge_reference.h，仅供 objectdata_test / merge_benchmark；当前生产算法继续与参考结果比较，不将参考算法链接进应用。

## 查询成本证据

新增 tools/configuration_benchmark.cpp / configuration_benchmark，仅在 BUILD_TESTING 中构建，不注册默认 CTest。参考路径重建此前全配置候选展开，和优化路径使用相同当前元数据序列化，因此只隔离候选列表策略的成本，不是对旧提交整个程序的基准。

1 万原始信号，每配置一条规则，查询全部摘要并使用一个活动配置。先比较所有摘要及所有配置的候选内容一致，再预热一次、交替顺序计时。Debug 合成结果：

| 配置数 | 全量展开中位 ms | 按需查询中位 ms | 全量候选条目 | 按需候选条目 |
| --- | --- | --- | --- | --- |
| 1 | 18.9594 | 18.1225 | 10,001 | 10,001 |
| 16 | 24.7876 | 19.7450 | 160,016 | 10,001 |
| 64 | 89.3774 | 60.5264 | 640,064 | 10,001 |

全部摘要/候选逐项一致；1 配置 15 次、16/64 配置各 7 次。候选条目是列表元素数，不等同实际进程内存；耗时不代表 GUI 事件延迟、帧率或真实数据表现。原始结果见[JSON](configuration-query-benchmark-2026-10-18.json)。

## 验证范围

- Debug 应用、核心套件、merge_benchmark、configuration_benchmark 构建；qmllint 必须零 warning。
- 核心回归：配置候选隔离、前序输出限制、重命名/删除、当前格式往返/旧格式拒绝、丢包单输出及双输出拒绝、取消/预算/依赖清理、实际 QML 规则编辑与航迹、CLI 集成。
- 合并参考回归保留非单调、重复、缺失、冲突、偏移、无效时间戳及取消检查。
- 最终 Debug 五套核心回归全部通过（5/5，40.12 秒）；实际程序 + 标准库 CLI 集成通过。CTest 缓存解释器在受限环境不可执行，CLI 脚本使用 PATH 中可运行的 Python，断言不变。
- merge_benchmark 的 legacy/shared 对同一合成重复列 CSV 输出 rawSignals、mergeRules、outputSamples、errorCount、sha256 一致；实际触发至少一条合并规则。
- qmllint 零 warning，git diff --check、变更 Markdown 本地链接均通过。
- 未进行 Release、真实 GPU、HiDPI、真实数据或部署验收。
