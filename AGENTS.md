# AGENTS.md — 给 AI 编码代理的项目导航

本文档面向**读代码、改代码、跑测试的 AI 代理**（Cursor/Codex/Claude 等）。请先读这一份，再按需深入 `docs/` 下的文档。

## 项目一句话

DataInspector：Windows 上面向工程时间序列数据的高性能查看器。Qt 6.8 Qt Quick Scene Graph 渲染，曲线用 GPU 三角带 + min/max 保峰值 LOD 绘制。当前分支是 Qt Quick 重构版，旧 QWidget/QCustomPlot 代码已全部移除。

## 文档体系（先读哪份）

| 目的 | 文档 |
| --- | --- |
| 总入口、功能概览、项目结构树 | `readme.md` |
| 文档总索引 | `docs/README.md` |
| 最终用户操作手册（不需要改代码时不用读） | `docs/user-guide/manual.md` |
| **改代码前必读**：模块划分与数据流 | `docs/architecture/overview.md` |
| **改渲染相关必读**：Store→LOD→Geometry→SG | `docs/architecture/render-pipeline.md` |
| 改加载/导出必读 | `docs/architecture/data-and-export.md` |
| 改并发/线程相关必读 | `docs/architecture/concurrency.md` |
| 构建、CMake、部署 | `docs/dev/build.md` |
| 测试套件与验证命令 | `docs/dev/testing.md` |
| QML 静态检查/类型注册 | `docs/dev/qml-tooling.md` |
| 工具链/环境坑 | `docs/dev/toolchain-troubleshooting.md` |
| 历史开发记录与旧计划（问题溯源用，勿据此实现） | `docs/dev-log/README.md` |

## 代码地图

```
qml/                          界面层（QML）
  Main.qml                    主窗口、工具栏、信号树、子图网格
  TrajectoryPlot.qml          二维/三维航迹界面与位置标记
  QuickPlot.qml               单个子图：坐标轴、游标、右键菜单
  PlotLegend.qml              图例
  PlotAxisArea.qml            X/Y 轴留白区交互
  Splash.qml                  启动画面
src/quick/
  main.cpp                    入口：Splash → Main 加载顺序、启动参数导入
  appcontroller.*             控制器：生命周期/加载/子图/会话（分文件）
  sessiondocument.*           会话 JSON 校验与原子读写
  signalmodel.*               信号树模型
  startupfiles.*              命令行参数解析
  dataloadworker.*            后台 CSV/TXT/MAT 解析
  xlsxreader.* / xlsxwriter.* XLSX 读写
  dataexportworker.*          后台导出调度
  matwriter.* / mat5streamwriter.*  MAT5 流式写入
  exporttable.h / exportvalidation.*  导出共用结构与时间基校验
  plotitem.*                  Scene Graph 曲线项、视图交互、节点提交
  plotitem_cursor.cpp         原始样本游标导航、读数
  plotitem_interaction.cpp    拾取、平移、滚轮与拖动
  plotitem_render.cpp         QSG 节点、几何上传、曲线层级
  trajectoryitem.*           航迹投影、后台任务、旋转/平移交互
  appcontroller_trajectory.cpp  轨迹绑定与时间游标联动
  qmltypes.h                  QML_FOREIGN 类型注册
  render/                     渲染内核（无 QQuickItem 依赖）
    trajectorybuilder.*      两/三轴时间基校验、空间 LOD、等比例投影
    plotseriesstore.*         不可变原始序列 Store、代际号
    plotrangeindex.*          512 样本分块极值 + 线段树
    plotlodbuilder.*          视窗/屏幕桶 min/max LOD
    plotlodscheduler.*        后台 LOD 调度（最多 2 线程）
    plotgeometrybuilder.*     LOD → 屏幕空间三角带
    plotaxisutils.*           浮点刻度、适应边距
tests/                        CTest 套件（清单见 docs/dev/testing.md）
tools/deploy_qt6.ps1          Windows 部署脚本
```

## 快速验证命令

```powershell
# 构建（Debug）
cmake --preset qt6-clang-debug
cmake --build --preset qt6-clang-debug

# QML 静态检查（MaxWarnings=0，任何 warning 都算失败）
cmake --build build_qt6-debug --target DataInspector_qmllint

# 测试（注意先设 Fusion 样式，离屏环境必需）
$env:QT_QUICK_CONTROLS_STYLE = 'Fusion'
ctest --test-dir build_qt6-debug --output-on-failure

# Release 同理，目录换成 build_qt6-release
```

- 默认 `ENABLE_MAT=ON`（仓库自带 LLVM-MinGW 17 兼容静态库）。
- GPU 光栅测试默认关闭；需要时 `-DENABLE_GPU_TESTS=ON`。
- 换构建目录后必须重新 configure + build `DataInspector_qmllint`，否则编辑器/工具读不到 `.qmltypes`。

## 改动时的硬约束

1. **不要引入 QCustomPlot 或任何新绘图库**；绘图路径必须是 Scene Graph。
2. **游标查询只用原始样本**，不用 LOD、不插值；查询走 `PlotSeriesStore`，不是 `PlotItem`。
3. **跨线程数据一律不可变快照**：Store 序列/索引不可变，画笔/偏移变更整体替换快照；渲染线程不碰 GUI 线程可变对象。`PlotItem` 的视图/游标状态在 `m_dataMutex` 保护下读写，`mousePressEvent` 的拖拽快照必须在锁内完成。
4. **QML 类型注册**走 `src/quick/qmltypes.h`（`QML_FOREIGN`），`Main.qml` 用 `required property AppController appController` 接收，不要用隐式上下文变量，也不要手工 `qmlRegisterType`。
5. **QML 文件显式 `pragma ComponentBehavior: Bound`**；qmllint 必须零 warning。
6. **新增功能同步更新文档**：功能 → `docs/user-guide/`，设计/内核 → `docs/architecture/`，构建/工具链 → `docs/dev/`；开发过程记录追加到 `docs/dev-log/`。
7. **已遗弃功能不要实现**：重放、Python API、`.mldatx` 视图文件。PNG 图片导出现已迁移，见 `docs/user-guide/image-export.md`。
8. **会话文件是版本化 JSON，结构校验严格**：改动格式必须同步 `sessiondocument.*` 与 `docs/user-guide/session.md`。
9. **数值解析统一 C locale**：CSV 数值不接受千位分组符；导出前校验同表信号时间基。

## 常见坑（详见 docs/dev/toolchain-troubleshooting.md）

- `windres: preprocessing failed.`：RC 编译器必须是 `llvm-windres.exe`（预设已钉死）；只 rebuild 不 reconfigure 无效。
- 部署包缺少 `imageformats/qsvg.dll` / `iconengines/qsvgicon.dll` 时 SVG 图标空白。
- 无清单 exe 可能被 Windows installer detection 误提权，导致无法拖放文件（UIPI）；`assets/DataInspector.manifest` 已声明 `asInvoker`。
- 改动 CMake 目标后记得同步 `DI_*_SOURCES` 列表与 `docs/dev/build.md`。

## 验证边界（诚实声明）

- **已评估并撤回的优化**：渲染锁外快照及多级冷 LOD 摘要试验见 [撤回决定与基准](docs/dev-log/render-lock-cold-lod-2026-10-09.md)，原始数据见 [JSON](docs/dev-log/render-lod-benchmark-2026-10-09.json)。局部视窗约慢 13%，增加索引内存/准备成本，整体交互收益不稳定；用户决定撤回。后续提出相同方向前必须先读记录，说明如何解决已测退化并提供新的对比证据，不要把它当作尚未尝试的优化建议，也不要仅凭全局冷 LOD 收益恢复实现。此结论针对该试验，不代表所有优化方向无效。

- 离屏测试/离屏启动 ≠ 真实 GPU 视觉回归；需要时用 `-DENABLE_GPU_TESTS=ON` 且需硬件 Scene Graph 后端。
- `docs/dev-log/` 里的开发记录与历史归档计划均为历史，勿据此判断现状。
