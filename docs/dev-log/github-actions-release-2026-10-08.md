# GitHub Windows 自动构建发布（2026-10-08）

新增 `.github/workflows/windows-release.yml`，main 推送更新 latest 预发布，v* 标签生成独立正式 Release；PR 只验证，不授予发布权限。使用标准 Windows runner 和现有 Qt 6.8.3 / LLVM-MinGW 17.0.6 工具链，保留 MAT 支持。

发布门槛包含 Release 构建、零 warning qmllint、离屏 CTest、清除 SDK 搜索路径后的部署启动检查。产物为包含运行依赖的 ZIP 和 SHA256 校验文件。新增 `tools/smoke_packaged_qt6.ps1`，复用已有部署脚本。

本地工作流静态检查和现有部署包启动检查的结果见本次交付说明；首次 GitHub 实际运行才验证远端 SDK 安装及完整流水线。未把离屏检查视为 GPU 或人工 GUI 验收。
