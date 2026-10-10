# v0.2.0 发布验证（2026-10-10）

本版本接续 v0.1.0，包含密集曲线边缘修复、精确游标输入、对象派生管理与飞机绑定，以及状态/引用收敛、规则依赖增量计算、独立模板 v2 和职责拆分。项目版本及用户更新记录均为 0.2.0。

代码整理提交：961a0d6。Release 配置启用 MAT，GPU 测试关闭；完整构建通过，DataInspector_qmllint 零 warning。Fusion/offscreen/software 完整 CTest 12/12 通过，75.87 秒；报告位于本地 build_qt6-release/release-test-results.xml。此前 Debug 全套 12/12 及静态检查也通过。

模板继续读取 v1，保存写入 v2，旧程序不能读取 v2。会话仍为 v10，兼容读取旧版本。稳定系列 ID 未迁移；撤回的锁外快照/冷 LOD 试验保持撤回。

远端发布由 .github/workflows/windows-release.yml 对主分支和 v0.2.0 标签构建，成功后生成便携 ZIP、SHA256SUMS 并发布正式 GitHub Release。工作流还会执行干净 SDK 搜索路径的部署包启动检查。真实硬件 GPU/帧率和人工 GUI 验收未执行，不将离屏测试作为这些验证的替代。

本地部署验证：tools/deploy_qt6.ps1 生成 dist/v0.2.0/DataInspector；smoke_packaged_qt6.ps1 清除 SDK 搜索路径后运行 15 秒，无依赖/QML 加载错误。此检查使用 Fusion/offscreen/software，不是硬件 GPU 或人工 GUI 验收。
