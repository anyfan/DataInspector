# 工程目录整理（2026-10-10）

分支：codex/project-directory-cleanup；起点为 v0.2.0 / 6cf59d5。此批整理目录和文档，不修改应用逻辑、会话或模板格式。

## 整理范围

- third_libs 仅保留构建使用的 matio、HDF5、zlib 目录，新增 README 说明版本、静态链接文件和工具链边界。无调用方的 QCustomPlot、Qt5 QuaZip、pybind11 ZIP 及空目录归入本地历史副本。
- 移出没有 CMake/QML/入口调用方的旧 assets/resources.qrc 和八个图标；当前图标继续由 DI_ICON_FILES 与 Windows RC 管理。
- 本机 build_ci 原内容是 actionlint/pip/Python 辅助工具及云端状态 JSON，没有 CMakeCache.txt；整体归档，为真正的 CI 构建目录留出位置。
- 旧 dist/DataInspector_last.exe 与 Enigma Virtual Box 项目 prj.evb 归档。现行部署目录、v0.1.0/v0.2.0 包、真实 test_file 数据、Debug/Release 构建树及本机编辑器配置保留原路径。
- .gitignore 按构建、产物/数据/归档、编辑器、Qt 自动生成文件分组，使用根目录路径。新增 .local/ 以及 Qt Creator 备份配置忽略项。
- 更新 AGENTS.md、readme.md、docs/README.md 和构建文档，新增 docs/dev/project-layout.md，说明现行目录职责和维护约定。

## 本地归档

使用 PowerShell 原生 Move-Item，同一文件系统内移动；逐项校验绝对来源和目标均处于工程目录且目标不存在，没有递归删除。归档位于 .local/archive/2026-10-10/，moves.json 保存 16 个原路径/归档路径。该目录不提交 Git，历史代码也可从旧提交查询。

## 验证

Debug（ENABLE_MAT=ON）configure 和完整构建通过；DataInspector_qmllint 零 warning；Fusion/offscreen/software 完整 CTest 12/12 通过，87.04 秒。40 个移出工作目录的已跟踪文件与 Git 原始内容逐个核对一致；现存 22 个图标全部有 CMake 或 Windows RC 引用。目录说明的相对链接可解析，本机归档/真实数据/产物的忽略规则有效；git diff --check 通过。本批未重跑 Release、真实 GPU、人工 GUI 或部署验收。
