# Y 计数丢包检测（2026-10-17）

扩展现有 packetloss 为 time、sequence、stride、rate，Y 模式手动绑定信号。固定抽样区分记录间隔与帧步长，rate 仅适用于累计实际接收计数。原始快照后台计算、取消/预算/下游失效沿用；正常回绕不计缺失，半范围及频率不一致拒绝计算，完整额外回绕无法从两点唯一识别。

配置贯通编辑器、控制器、复制、CLI schema、失效键及 v14 会话。旧会话默认 time，自动识别只创建 X 检测。编辑器增加滚动以容纳参数；重复采样计数为运行时统计。

验证：Debug 主程序及相关测试构建成功，DataInspector_qmllint 零 warning；appcontroller_test、session_test、objectdata_test、automation_test、trajectory_test 全部通过（最终一轮 5/5，31.97 秒）。涵盖真实 QML 编辑器提交、CLI rule_update、v13 默认迁移、v14 严格类型与参数重算、保存恢复和复制。

首轮因沙箱临时目录无法写入而失败；将当前测试进程 TEMP/TMP 指向 build_qt6-debug/test-temp 后重跑通过。新增重算测试最初缺少已发布输出快照，按实际 Store 生命周期补齐 fixture 后通过。未进行人工 GUI、Release、GPU 或部署验收。
