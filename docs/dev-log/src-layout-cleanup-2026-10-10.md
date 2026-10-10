# C++ 源码按职责整理（2026-10-10）

接续工程目录整理，仍位于 codex/project-directory-cleanup；保留先前未提交的目录清理，不修改已发布 v0.2.0 主分支/标签。

将 src/quick 根目录的 46 个代码文件移入 controller、io、models、state、items。现有 render 内核目录保持原位；根目录仅保留 main.cpp、startupfiles.cpp/.h、qmltypes.h 和源码导航 README。

控制器按现有职责实现继续共用同一个 AppController，不增加状态副本或改变逻辑。跨模块包含改为显式路径，同目录保留本地头文件名。同步 CMake 全部 DI_*_SOURCES 和单独测试目标的源路径、应用/测试/性能基准头文件引用及 QML_FOREIGN 元数据输入；头文件搜索根仍为 src/quick，不添加旧路径转发头。

更新 AGENTS.md、readme.md、现行架构/构建/目录说明及源码导航。历史日志保留旧路径，避免改写历史证据。

46 个迁移代码文件逐一反向规范化 include 路径后与 Git 原内容完全相同，CMake 也确认只变更源文件路径。所有项目头文件可从自身目录或统一 src/quick 搜索根解析，现行导航链接有效，git diff --check 通过。Debug（ENABLE_MAT=ON）configure/完整构建通过，DataInspector_qmllint 零 warning；Fusion/offscreen/software 完整 CTest 12/12 通过，92.99 秒，包含源码与编译 QML、会话/模板、对象规则及数据导出。此批未执行 Release、真实 GPU、人工 GUI 或部署验收。
