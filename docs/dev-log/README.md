# 开发日志索引

本目录保留历次功能开发、缺陷修复和工程整理的原始记录，也收纳更早的**历史归档**（已完成/已被取代的旧文档）。每篇对应一次具体工作，记录了当时的需求、根因、改动文件与验证结果。长期有效的结论已吸收到 Wiki 其他章节；这里按主题分组便于回查。

> **给 AI 代理**：以下 `mcp-*.md` 与归档文档均为历史记录，不代表当前代码状态；实现现状以 `docs/` 下对应主题的现行文档和源码为准。**不要依据归档里的计划重新实现。**

## 用户功能与交互

| 文档 | 主题 |
| --- | --- |
| [对象与派生数据调研](object-derived-data-research-2026-10-10.md) | 通用对象、仅对象派生、位拆分、时间语义与分期建议 |
| [对象与派生数据首版](object-derived-data-implementation-2026-10-10.md) | 后台计算、状态字与公式、统一管理、v7 会话、关联航迹和验证边界 |
| [关于页面](about-page-2026-10-09.md) | 构建时版本/Git 信息、离线更新记录、复制诊断信息 |
| [构建、交互及会话优化](optimization-batch-2026-10-08.md) | 共用编译库、搜索与拖拽、会话重定位、PNG 导出、LOD 缓存和性能验证 |
| [信号树展开及滚动稳定性](signal-tree-scroll-stability-2026-10-08.md) | 局部行通知、保留浏览位置、固定滚动区域及拖动手势隔离 |
| [信号树拖拽到子图](signal-tree-drag-2026-10-08.md) | 目标高亮、指定子图添加、重复与取消行为、鼠标事件回归 |
| [信号树分组标题与左对齐](signal-tree-sections-2026-10-08.md) | 取消累计缩进、分组路径标题条、吸顶路径和深色主题 |
| [飞机辨识度与姿态表单显示修复](trajectory-attitude-visual-form-2026-10-02.md) | 缩小飞机、机身机翼尾翼轮廓、对比描边、字段/滚动与下拉文字 |
| [多航迹与实际三维姿态实施](trajectory-multi-attitude-2026-10-02.md) | 公共原点、活动航迹、测量姿态、v6 会话与回归边界 |
| [多航迹与姿态显示工作量评估](trajectory-multi-attitude-estimate-2026-10-02.md) | 实施前的历史范围估算：单图叠加、公共原点、姿态信号与人天/内存边界 |
| [航迹默认选入绑定与角度轴色](trajectory-default-bindings-colors-2026-10-02.md) | 默认经纬度、勾选补空轴、保留手动绑定、三轴角度独立着色 |
| [航迹 NED 坐标与角度零基准](trajectory-ned-angle-reference-2026-10-02.md) | 北内、东右、地下为零姿态；世界轴绝对读数与标准视图回归 |
| [航迹方向跳变与绝对视角读数](trajectory-direction-pose-2026-10-02.md) | 环中心死区、跨手势旧帧隔离、当前 X/Y/Z 姿态角 |
| [航迹角落旋转环与自动收起](trajectory-corner-gizmo-2026-10-02.md) | 与方向坐标轴合并、靠近展开/离开收起、保留绘图区中心支点 |
| [航迹 CAD 三轴旋转操纵器](trajectory-cad-gizmo-2026-10-02.md) | 直接拖彩色轴环、环内自由旋转、环外平移、侧视回退、无模式切换 |
| [航迹中心旋转与轴约束](trajectory-axis-rotation-2026-10-02.md) | 虚拟轨迹球、自由/X/Y/Z 模式、中心参考、Shift 平移与 Alt 旋转 |
| [航迹高倍缩放与编译界面刷新](trajectory-zoom-binding-2026-10-02.md) | 解除四视口平移限制、滚轮后平移保留缩放、显式绑定刷新、编译 QML 回归 |
| [航迹可用信号与小坐标轴拖动](trajectory-candidates-triad-2026-10-02.md) | 先加入子图再绑定轴、局部候选下拉框、小坐标轴左键旋转、v5 会话 |
| [航迹自适应、鼠标路由与绑定](trajectory-fit-input-binding-2026-10-02.md) | 零定位只排除自适应、统一鼠标输入、指针缩放、轴指定替换与名称识别 |
| [航迹信号树选择与当前视图旋转](trajectory-selection-camera-2026-10-02.md) | 直接打开、独立勾选状态、左键平移、中键自由旋转、v4 会话 |
| [航迹交互性能与双参数](trajectory-interaction-2026-10-02.md) | 空间 LOD 加速旋转、方向修正、两轴平面航迹、扩大子图画布 |
| [飞机位置投影与方向坐标轴](trajectory-geographic-projection.md) | 经纬度/高度米制投影、移除外框、随视角旋转的小坐标轴、v3 会话 |
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
| [密集曲线中间缩放的残留边缘凸点](dense-cross-bucket-zoom-2026-10-09.md) | 跨桶、多尺度与选中线宽判定；GPS 和 H_milliSec 原始数据 GPU 回归 |
| [渲染持锁与冷 LOD 优化试验（已撤回）](render-lock-cold-lod-2026-10-09.md) | 不可变渲染快照、多级 min/max 摘要；百万/千万点基准、局部退化、成本、验证边界与撤回决定；附原始 JSON |
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

- [构建自动并行（2026-10-08）](build-parallel-2026-10-08.md)：Debug/Release 预设按本机逻辑 CPU 数设置并行任务。

- [GitHub Windows 自动构建发布（2026-10-08）](github-actions-release-2026-10-08.md)：main 开发版、版本标签正式版、MAT 支持及部署启动门槛。

| 文档 | 主题 |
| --- | --- |
| [应用图标重绘与 exe 图标嵌入](mcp-application-icon.md) | SVG → 多分辨率 ICO，.rc 资源接入 |
| [Release 构建 windres: preprocessing failed](mcp-windows-resource-toolchain.md) | CMAKE_RC_COMPILER 钉到 llvm-windres |
| [部署包 SVG 图标空白](mcp-release-svg-icons.md) | 补 imageformats/qsvg.dll 与 iconengines |
| [启动过渡动画与启动性能](mcp-startup-transition-animation.md) | Splash.qml 120ms + 180ms 淡出 |

## 工程质量

| 文档 | 主题 |
| --- | --- |
| [三维运动轨迹子图](trajectory-mode-2026-10-01.md) | XYZ、后台投影、时间联动、v2 会话 |
| [代码分类与历史设计清理（2026-10-01）](code-organization-cleanup-2026-10-01.md) | PlotItem 职责拆分、废弃混色和重复游标状态清理 |
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

- [状态与计算债务清理（第一批，2026-10-10）](state-debt-cleanup-2026-10-10.md)：航迹唯一状态、统一引用更新、对象局部计算及隐藏时间图 LOD 暂停；包含尚未实施项和验证边界。

- [状态与计算债务清理（剩余三项，2026-10-10）](state-debt-cleanup-completion-2026-10-10.md)：规则依赖增量计算、独立模板 v2 与旧版迁移、QML/控制器拆分和完整回归。
