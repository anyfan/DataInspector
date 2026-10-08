# GitHub Windows 自动构建发布（2026-10-08）

新增 `.github/workflows/windows-release.yml`，main 推送更新 latest 预发布，v* 标签生成独立正式 Release；PR 只验证，不授予发布权限。使用标准 Windows runner 和现有 Qt 6.8.3 / LLVM-MinGW 17.0.6 工具链，保留 MAT 支持。

首次远端验证发现 SVG 已属于 Qt 基础包、CMake 的 RC 编译器路径需要正斜杠，以及多工作表回归依赖未提交的本机 XLSX。配置改为安装基础包并规范路径；测试改用自动生成的三工作表/80 信号数据，保留工作表完整导入、信号数及汇总行数断言。配置/测试失败输出写入 GitHub 检查注释，便于诊断。

发布门槛包含 Release 构建、零 warning qmllint、离屏 CTest、清除 SDK 搜索路径后的部署启动检查。产物为包含运行依赖的 ZIP 和 SHA256 校验文件。新增 `tools/smoke_packaged_qt6.ps1`，复用已有部署脚本。

本地工作流静态检查和现有部署包启动检查的结果见本次交付说明；首次 GitHub 实际运行才验证远端 SDK 安装及完整流水线。未把离屏检查视为 GPU 或人工 GUI 验收。
