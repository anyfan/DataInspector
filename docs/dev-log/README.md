# 开发日志索引

本目录保留历次功能开发、缺陷修复和工程整理的原始记录。每篇对应一次具体工作，记录了当时的需求、根因、改动文件与验证结果。长期有效的结论已吸收到 Wiki 其他章节；这里按主题分组便于回查。

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
