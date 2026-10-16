# 工程目录与本地文件

| 路径 | 职责 | Git 管理 |
| --- | --- | --- |
| `src/quick/` | 应用入口、启动参数与 QML 注册 | 是 |
| `src/quick/controller/` | 控制器各职责实现 | 是 |
| `src/quick/automation/` | 本机 JSON CLI 服务、认证与后台数据分析 | 是 |
| `src/quick/io/` | 数据读写、加载/导出任务及共用数据结构 | 是 |
| `src/quick/models/` | 信号树、元数据、对象定义和派生规则 | 是 |
| `src/quick/state/` | 会话/模板配置及 JSON 读写校验 | 是 |
| `src/quick/items/` | Scene Graph 项和交互 | 是 |
| `src/quick/render/` | 不依赖 QQuickItem 的绘图内核 | 是 |
| `qml/` | 主窗口与独立界面组件 | 是 |
| `assets/` | 应用图标、工具栏 SVG、Windows RC/manifest | 是 |
| `third_libs/` | 当前 MAT 静态库与头文件，清单见目录 README | 是 |
| `tests/` | 自动化测试，默认在测试内生成数据 | 是 |
| `tools/` | JSON CLI、部署、安装包启动检查、性能基准入口 | 是 |
| `cmake/` | 构建元数据生成与模板 | 是 |
| `.github/` | 云端构建和发布工作流 | 是 |
| `docs/user-guide/` | 用户操作与版本记录 | 是 |
| `docs/architecture/` | 现行架构与数据流 | 是 |
| `docs/dev/` | 构建、验证及工具链说明 | 是 |
| `docs/dev-log/` | 开发过程、原始测量和历史计划 | 是 |
| `build_qt6-debug/`、`build_qt6-release/` | 本地 CMake 构建及测试输出 | 否 |
| `build_ci/` | GitHub Actions 构建目录；本地 CI 复现也可使用 | 否 |
| `dist/<版本>/` | 按版本保存的便携包、ZIP 和 SHA256 | 否 |
| `dist/DataInspector/` | 部署脚本/CI 默认的便携包工作目录 | 否 |
| `test_file/` | 本机真实大数据样本 | 否 |
| `.local/` | 本机工具、工作文件和可恢复归档 | 否 |

根目录保留项目入口 `readme.md`、代理导航 `AGENTS.md`、CMake 配置和 `.gitignore`。Qt Creator、VS Code 及 Qt QML 工具生成的本机配置也忽略，不用于部署。

## 维护约定

- 当前图标由 `CMakeLists.txt` 的 `DI_ICON_FILES` 和 `di_add_icon_resources()` 统一生成 `qrc:/icons`；Windows 图标由 `DataInspector.rc` 引入。不要再维护旧版独立资源清单。
- 构建目录包含绝对路径，不直接移动或改名现有构建树。需要更换时，在新位置 configure/build，并重新生成 `.qmltypes` 和 QML 工具配置。
- 真实数据与已发布包按现有路径保留。默认测试不依赖 `test_file/`；新增小型固定回归数据应放在 `tests/` 下，并在测试清单记录。
- 本机 CI 辅助工具放在 `.local/`，避免与 `build_ci/` 构建树混用。历史包和旧打包器项目可放在 `.local/archive/<日期>/`。
- 历史依赖移出源码树后仍可通过 Git 查询。开发记录和原始基准保留在 `docs/dev-log/`，不当作现行实现。

2026-10-10 整理保留了本地副本：`.local/archive/2026-10-10/moves.json` 记录原路径和归档路径。恢复单项时先确认目标不存在，再按记录移动回原位置；归档不提交 Git。

C++ 文件详细入口与 include 约定见[源码导航](../../src/quick/README.md)。跨模块引用显式写子目录，CMake 只保留统一的 src/quick 头文件搜索根；新增文件同步现有 DI_*_SOURCES。
