# 开发日志索引

本目录保留历次功能开发、缺陷修复和工程整理的原始记录，也收纳更早的**历史归档**（已完成/已被取代的旧文档）。每篇对应一次具体工作，记录了当时的需求、根因、改动文件与验证结果。长期有效的结论已吸收到 Wiki 其他章节；这里按主题分组便于回查。

> **给 AI 代理**：以下 `mcp-*.md` 与归档文档均为历史记录，不代表当前代码状态；实现现状以 `docs/` 下对应主题的现行文档和源码为准。**不要依据归档里的计划重新实现。**

## 用户功能与交互

| 文档 | 主题 |
| --- | --- |
| [会话保存与恢复](mcp-session-save-restore.md) | `.disession` JSON 格式、事务式恢复、子图生命周期 |
| [归一化 Y 轴](mcp-normalize-y.md) | 子图级 0..1 缩放，游标仍显示原始值 |
| [拖到 exe 启动加载](mcp-startup-file-arguments.md) | 命令行参数解析、会话文件优先、去重 |
| [UAC 提权与拖放失效](mcp-elevation-and-drag-drop.md) | 无清单 exe 被 installer detection 提权、asInvoker 清单 |
| [重叠曲线混色 · 空格自适应 Y · 信号树粘性层级](mcp-overlap-blend-space-fit-sticky-tree.md) | min/max 混合材质、信号树吸顶表头、线型预览改 Shapes |
| [子图右键菜单、图例行距与 Y 轴留白](mcp-plot-context-menu-legend-spacing.md) | 自适应 Y 留白、清除子图信号、游标 25/75% 初始化 |
| [轴区滚轮以指针为中心缩放](mcp-axis-wheel-zoom-anchor.md) | X/Y gutter zoom anchor |
| [光标键盘左右步进](mcp-cursor-keyboard-step.md) | ←/→ 在原始样本间移动，清空信号后的焦点修复 |
| [自适应菜单拆分当前/全部](mcp-fit-view-scopes.md) | fitScopeIncludes / fitSourceRows |
| [最大化子图的 X 自适应范围](mcp-maximized-subplot-fit.md) | soloPlotIndex |

## 渲染内核与性能

| 文档 | 主题 |
| --- | --- |
| [密集曲线缩放几何](mcp-dense-curve-zoom-geometry.md) | 三角形条带改为常宽四边形三角形列表，消除楔形填充 |
| [密集曲线圆角方案（已撤回）](mcp-dense-curve-round-joins.md) | 首次方案与历史验证，现行实现见补充包络记录 |
| [密集预览修复紧急回退](mcp-dense-preview-emergency-rollback.md) | 撤回 9aa5769，优先恢复普通曲线连续性 |
| [密集预览补充包络](mcp-dense-preview-additive-envelope.md) | 保留全部原折线，GPS 边缘与发动机跳变双数据回归 |
| [PlotItem 输入线程安全](mcp-plotitem-input-thread-safety.md) | mousePress/mouseMove 锁内快照 |
| [来源隔离、Y 范围索引、MAT 流式导出](mcp-source-isolation-and-export-performance.md) | plotrangeindex 线段树、mat5streamwriter、系列 ID 哈希 |
| [代码审查：会话边界、数值稳定性与查询优化](mcp-review-numeric-and-query.md) | 14 色调色板越界、大时间戳刻度、LOD 浮点桶、时间范围摘要 |
| [数据导入与绘图仓库缺陷审查](mcp-bug-audit-and-fixes.md) | MAT 复数/稀疏防护、CSV 引号一致性、点数组单调性 |

## 数据与导出

| 文档 | 主题 |
| --- | --- |
| [信号重命名、MAT 导出与线型预览单击编辑](mcp-signal-rename-mat-export.md) | matwriter、pN/pN_title 布局、renameSignal |

## 构建、部署与工具链

| 文档 | 主题 |
| --- | --- |
| [应用图标重绘与 exe 图标嵌入](mcp-application-icon.md) | SVG → 多分辨率 ICO，.rc 资源接入 |
| [Release 构建 windres: preprocessing failed](mcp-windows-resource-toolchain.md) | CMAKE_RC_COMPILER 钉到 llvm-windres |
| [部署包 SVG 图标空白](mcp-release-svg-icons.md) | 补 imageformats/qsvg.dll 与 iconengines |
| [启动过渡动画与启动性能](mcp-startup-transition-animation.md) | Splash.qml 120ms + 180ms 淡出 |

## 工程质量

| 文档 | 主题 |
| --- | --- |
| [工程代码整理](mcp-code-cleanup.md) | 删除旧 QWidget 代码、AppController/QML 拆分、CMake 函数化 |
| [QML 静态分析警告修复](mcp-qml-static-analysis.md) | qmltypes.h、pragma Bound、MaxWarnings=0 |

## 历史归档（更早的旧文档，勿据此实现）

| 文件 | 内容 | 状态 | 现行对应文档 |
| --- | --- | --- | --- |
| `refactoring-status.md` | 2026-09-13 重构进度快照（加载职责分离、子图生命周期、游标性能、异步 LOD 迁移） | **历史快照，已过时** | [架构总览](../architecture/overview.md)、[并发](../architecture/concurrency.md) |
| `renderer_research.md` | 2026-09-07 绘图后端选型调研（QCustomPlot → Qt Quick Scene Graph 的决策依据） | **决策留档，结论已落地** | [渲染管线](../architecture/render-pipeline.md) |
| `superpowers/plans/2026-09-08-quick-cursor-axis-legend-subplots.md` | 游标/坐标轴/图例/多子图迁移实施计划 | **已完成** | [视图操作](../user-guide/view-operations.md) |
| `superpowers/specs/2026-09-08-quick-cursor-axis-legend-subplots-design.md` | 上述迁移的设计说明 | **已完成** | [视图操作](../user-guide/view-operations.md) |
| `superpowers/plans/2026-09-10-render-core-separation.md` | 渲染内核拆分（Store/LOD/Geometry 分层）实施计划 | **已完成** | [渲染管线](../architecture/render-pipeline.md) |
| `superpowers/specs/2026-09-10-render-core-separation-design.md` | 上述拆分的设计说明 | **已完成** | [渲染管线](../architecture/render-pipeline.md) |

历史归档注意事项：

1. **不要把归档计划当成待办**：`superpowers/plans/` 里的 checkbox 任务是历史实施清单，均已执行完毕，不要据此重复实现。
2. **旧路径引用已失效**：归档中的 `src/plot/*`、`src/core/*` 等旧 QWidget/QCustomPlot 路径已在提交 `8d2b6bb` 后移除；现行源码在 `src/quick/` 与 `qml/`。
3. **描述以现行文档为准**：例如 `refactoring-status.md` 中的"后续阶段"（图片导出、重放、Python API）并非当前计划——重放/Python API/`.mldatx` 已正式遗弃，见根 `readme.md`。
