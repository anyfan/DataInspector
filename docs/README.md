# DataInspector 文档

DataInspector 是面向工程时间序列数据的高性能查看器，基于 Qt 6.8 Qt Quick Scene Graph 渲染。

## 我是 AI 编码代理（Cursor / Codex / Claude 等）

先读根目录 [`AGENTS.md`](../AGENTS.md)：它包含代码地图、验证命令、改动硬约束和常见坑，是本仓库对代理的官方导航。

## 我只是想用这个软件

→ **[《使用指南》](user-guide/manual.md)**：从启动、加载数据、画曲线、缩放游标到导出和保存会话的完整操作手册，不涉及任何代码。

## 其他入口

- **想自己构建/部署/二次开发？** → [开发者指南](dev/)。
- **想理解代码结构与渲染设计？** → [架构](architecture/)。
- **想看某次功能/修复的来龙去脉？** → [开发日志](dev-log/)。
- **想看已完成的历史设计/旧计划？** → [开发日志](dev-log/)（含历史归档，标注了状态与现行对应文档）。

---

## 用户指南（参考）

| 文档 | 内容 |
| --- | --- |
| [**使用指南**](user-guide/manual.md) | **最终用户操作手册（推荐入口）** |
| [快速上手](user-guide/quick-start.md) | 构建、部署、启动、加载第一个数据文件（偏工程） |
| [数据格式](user-guide/data-formats.md) | CSV/TXT/XLSX/MAT 的导入规则与导出说明 |
| [三维运动轨迹](user-guide/trajectory.md) | 二维/三维航迹、经纬度投影、子图信号绑定、视角与时间游标 |
| [视图操作](user-guide/view-operations.md) | 平移/缩放/自适应/游标/快捷键表 |
| [信号树与样式](user-guide/signals-and-styling.md) | 勾选、重命名、时间偏移、颜色/线宽/线型 |
| [会话保存与恢复](user-guide/session.md) | `.disession` 文件、保存内容、恢复流程与边界 |

## 架构

| 文档 | 内容 |
| --- | --- |
| [整体架构](architecture/overview.md) | 模块划分、数据流、进程/线程模型概览 |
| [渲染管线](architecture/render-pipeline.md) | Store → LOD → Geometry → Scene Graph，min/max 降采样 |
| [数据加载与导出](architecture/data-and-export.md) | 后台加载、分块极值索引、来源隔离、XLSX/MAT 导出 |
| [并发与线程安全](architecture/concurrency.md) | GUI/工作线程边界、快照不可变性、锁的使用 |

## 开发者指南

| 文档 | 内容 |
| --- | --- |
| [构建](dev/build.md) | 工具链、CMake 预设、部署脚本 |
| [工具链排错](dev/toolchain-troubleshooting.md) | windres、UAC/manifest、SVG 插件、图标等常见坑 |
| [QML 工具与静态检查](dev/qml-tooling.md) | qmllint、qmltypes、编辑器导入路径 |
| [测试](dev/testing.md) | CTest 套件一览、GPU 测试开关 |

## 开发日志与历史归档

[`dev-log/`](dev-log/) 下保留了历次功能开发与缺陷修复的原始记录（`mcp-*.md`），按主题索引见 [dev-log/README.md](dev-log/README.md)。这些记录是当时实现的第一手说明，已被上面的 Wiki 页面吸收为长期文档；遇到"为什么这么做"的问题时可以回查。

该目录还收纳了更早的**历史归档**：重构进度快照（`refactoring-status.md`）、渲染器选型调研（`renderer_research.md`），以及 superpowers 时代的实施计划/设计（`superpowers/plans` + `superpowers/specs`）。**这些不是待办，勿据此实现**；状态与现行对应文档见 [dev-log/README.md](dev-log/README.md) 末尾的"历史归档"小节。
