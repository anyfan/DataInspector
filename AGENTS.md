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
| 工程目录、第三方依赖、本地文件 | `docs/dev/project-layout.md` |
| 构建、CMake、部署 | `docs/dev/build.md` |
| 测试套件与验证命令 | `docs/dev/testing.md` |
| Agent 调用 CLI、数据分析与软件设置 | `docs/user-guide/automation-cli.md`、`docs/architecture/automation.md` |
| QML 静态检查/类型注册 | `docs/dev/qml-tooling.md` |
| 工具链/环境坑 | `docs/dev/toolchain-troubleshooting.md` |
| 历史开发记录与旧计划（问题溯源用，勿据此实现） | `docs/dev-log/README.md` |

## 代码地图

```
qml/                          界面层（QML）
  Main.qml                    主窗口、工具栏、拖拽协调、子图网格
  SignalBrowser.qml           信号树、搜索、导航及拖拽请求
  SessionDialogs.qml          会话/模板文件选择、匹配与保存提示
  ObjectManager.qml           对象与派生规则管理
  TrajectoryPlot.qml          二维/三维航迹界面与位置标记
  QuickPlot.qml               单个子图：坐标轴、游标、右键菜单
  PlotLegend.qml              图例
  PlotAxisArea.qml            X/Y 轴留白区交互
  Splash.qml                  启动画面
src/quick/
  main.cpp                    入口、Splash → Main 加载顺序
  startupfiles.*              启动参数解析
  qmltypes.h                  QML_FOREIGN 类型注册
  automation/                 本机 CLI、命令 schema、任务/幂等与合并检查计划
  controller/                 AppController 按职责拆分的实现
    appcontroller.*           生命周期与公共接口
    appcontroller_loading.cpp 加载/移除与导出入口
    appcontroller_plots.cpp   子图、布局、自适应
    appcontroller_trajectory.cpp  航迹绑定与时间联动
    appcontroller_session.cpp 完整会话恢复
    appcontroller_templates.cpp  模板匹配、应用、撤销
    appcontroller_objects.cpp 对象定义操作
    appcontroller_objectevaluation.cpp  规则增量计算调度
    appcontroller_automation.cpp  批量飞机配置、映射检查、规则查询与逻辑结果
    appcontroller_image.cpp / appcontroller_about.cpp  图片与关于信息
  io/                         后台加载/导出、CSV/TXT/MAT/XLSX 读写
    dataloadworker.* / dataexportworker.*  工作线程调度
    xlsxreader.* / xlsxwriter.*  XLSX 读写
    matwriter.* / mat5streamwriter.*  MAT 流式写入
    loadedtable.h / exporttable.h / exportvalidation.cpp  共用数据与校验
  models/                     信号树、元数据、对象定义与派生规则
    signalmodel.* / signalmetadata.* / objectdefinition.* / mergetimeline.*
  state/                      会话与模板值配置、JSON 校验和原子读写
    sessiondocument.* / viewconfiguration.* / viewtemplatedocument.*
    jsonvalidation.h
  items/                      Qt Quick Scene Graph 项及交互
    plotitem.* / plotitem_cursor.cpp / plotitem_interaction.cpp / plotitem_render.cpp
    trajectoryitem.*
  render/                     无 QQuickItem 依赖的绘图内核
    plotseriesstore.* / plotrangeindex.* / plotlodbuilder.* / plotlodscheduler.*
    plotgeometrybuilder.* / plotaxisutils.* / trajectorybuilder.*
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
