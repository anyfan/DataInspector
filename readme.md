# DataInspector（Qt Quick 版）

面向工程时间序列数据的高性能查看器。绘图区基于 Qt 6.8 Qt Quick Scene Graph：曲线用 GPU 三角带绘制，视图缩放时按屏幕宽度做 min/max 保峰值降采样，缺失值自动断线。

当前支持二维/三维轨迹混合子图、游标、会话保存恢复、时间偏移、信号重命名、XLSX/MAT 导入与导出。图片导出待迁移；**重放、Python API、`.mldatx` 视图文件已遗弃，不再计划实现**。

## 下载 Windows 版

[main 开发版下载](https://github.com/anyfan/DataInspector/releases/tag/latest) · [所有正式版本](https://github.com/anyfan/DataInspector/releases) · [自动构建状态](https://github.com/anyfan/DataInspector/actions/workflows/windows-release.yml)

下载 `DataInspector-windows-x64.zip`，解压整个目录后运行 `DataInspector.exe`，无需另装 Qt。每次推送 `main` 会在构建和检查通过后更新开发版；推送 `v*` 标签发布正式版。自动发布配置见 [构建文档](docs/dev/build.md#github-自动构建与发布)。

## 文档

完整文档已迁移到 **[`docs/`](docs/README.md)**：

- 用户指南：[**使用手册（最终用户）**](docs/user-guide/manual.md) · [快速上手](docs/user-guide/quick-start.md) · [数据格式](docs/user-guide/data-formats.md) · [视图操作](docs/user-guide/view-operations.md) · [航迹](docs/user-guide/trajectory.md) · [信号树与样式](docs/user-guide/signals-and-styling.md) · [会话](docs/user-guide/session.md)
- 架构：[总览](docs/architecture/overview.md) · [渲染管线](docs/architecture/render-pipeline.md) · [数据加载与导出](docs/architecture/data-and-export.md) · [并发](docs/architecture/concurrency.md)
- 开发：[构建](docs/dev/build.md) · [工具链排错](docs/dev/toolchain-troubleshooting.md) · [QML 工具](docs/dev/qml-tooling.md) · [测试](docs/dev/testing.md)
- 历史记录：[开发日志](docs/dev-log/README.md)

## 快速构建

```powershell
cmake --preset qt6-clang-debug
cmake --build --preset qt6-clang-debug
# 产物：build_qt6-debug/bin/DataInspector.exe
```

要求：Windows 10/11、Qt 6.8.3 `llvm-mingw_64`、LLVM-MinGW 17、CMake 3.25+、Ninja。详见 [快速上手](docs/user-guide/quick-start.md)。

## 主要功能

- CSV/TXT/XLSX/MAT 后台批量加载，支持多选打开、窗口拖放、拖到 exe 启动。
- 1×1 ~ 2×2 快捷布局与 1–8 行/列自定义布局，改变布局保留信号绑定。
- 子图可切换二维/三维航迹，默认经纬度模式；信号树按选入顺序填充空轴，已有绑定保留，顶部下拉框可调整来源。支持空间 XYZ 和 NED（北/东/向下）经纬度/高度米制投影；经纬度 `(0,0)` 不参与整体自适应，原始读数与导出保留。
- 航迹 CAD 三轴旋转环与左下方向坐标轴合并，靠近展开、离开收起；直接拖环绕固定 X/Y/Z 轴旋转，环内、中键或 Alt+左键自由旋转，普通左键或 Shift+左键平移。旋转围绕子图显示中心，短暂显示中心参考；角落当前绝对视角读数分别使用轴色，支持鼠标锚点缩放与预设视角。空间 LOD 加速旋转，时间游标定位原始样本。详见 [航迹指南](docs/user-guide/trajectory.md)。
- 鼠标拖动平移、滚轮双轴缩放（轴区滚轮以指针为中心单轴缩放）、框选。
- 密集振荡预览在保留原折线的基础上补充极值包络，减少上下沿的伪凸点；普通阶跃、单调变化和孤立尖峰保持原绘制路径。
- 单/双垂直游标、原始样本读数、ΔT、跨子图同步；←/→ 在原始样本间步进。
- 信号树按 文件 → `pN` 表 → 信号 分层；支持重命名、时间偏移、颜色/线宽/线型。
- 14 色固定调色板；重叠曲线按可见区间 `mean(abs(y))` 排序，小幅值置顶（保留原色）。
- `Ctrl+Z` 回退视图操作；`Ctrl+S` 会话保存，`.disession` 版本化 JSON。
- 导出 XLSX（多工作表、超行拆分）或 MAT（Level 5 流式写入），支持全部数据或当前子图绘制信号。
- 浅色/深色主题；信号树可隐藏、可搜索、滚动吸顶显示层级。

## 项目结构

```text
qml/
  Main.qml                 主窗口、工具栏、信号树、子图网格
  TrajectoryPlot.qml       二维/三维航迹控制、坐标标签与位置标记
  QuickPlot.qml            单个子图：坐标轴、游标、右键菜单
  PlotLegend.qml           图例换行、拖拽跨图、右键菜单
  PlotAxisArea.qml         X/Y 轴留白区悬停/框选/滚轮
  Splash.qml               启动画面
src/quick/
  main.cpp                 入口、Splash→Main 加载顺序
  appcontroller.*          控制器（按职责拆分 loading/plots/trajectory/session）
  sessiondocument.*        会话 JSON 校验与原子读写
  signalmodel.*            QML 信号树模型
  signalmetadata.*         信号元数据
  startupfiles.*           命令行参数解析
  dataloadworker.*         后台 CSV/TXT/MAT 解析
  xlsxreader.*             XLSX 解析
  dataexportworker.*       后台导出调度
  xlsxwriter.*             XLSX 写入
  matwriter.* / mat5streamwriter.*  MAT5 流式写入
  exporttable.h / exportvalidation.cpp  导出共用结构与时间基校验
  plotitem.*               Scene Graph 曲线项、视图交互、节点提交
  plotitem_cursor.cpp      原始样本游标导航、读数
  plotitem_interaction.cpp 拾取、平移、滚轮与拖动
  plotitem_render.cpp      QSG 节点、几何上传、曲线层级
  trajectoryitem.*         后台轨迹投影、Scene Graph 绘制与相机交互
  appcontroller_trajectory.cpp  轨迹绑定与时间联动
  qmltypes.h               QML_FOREIGN 类型声明
  render/
    trajectorybuilder.*    时间基校验、经纬度投影、空间 LOD 与相机投影
    plotseriesstore.*      不可变原始序列 Store、代际号
    plotrangeindex.*       512 样本分块极值 + 线段树
    plotlodbuilder.*       视窗/屏幕桶 min/max LOD
    plotlodscheduler.*     后台 LOD 调度（最多 2 线程）
    plotgeometrybuilder.*  LOD → 屏幕空间三角带
    plotaxisutils.*        浮点刻度生成、适应边距
tests/                     CTest 套件（详见 docs/dev/testing.md）
tools/deploy_qt6.ps1       Windows 部署脚本
assets/                    图标、.rc、.manifest
docs/                      Wiki（本文件的详细内容都在这里）
```

旧版 QWidget/QCustomPlot 实现（`src/core`、`src/plot`、`src/ui`、`src/script`、`src/data`）已在提交 `8d2b6bb` 后移除，历史见 git。

## 当前限制

- 航迹子图可叠加多条独立航迹，共用地理参考原点、空间范围和相机；支持活动选择、名称、颜色/线宽、显示/隐藏，以及每条航迹的测量欧拉角或四元数姿态。固定屏幕大小的简化飞机随共享原始时间游标显示，角落视角仍只表示观察相机。单子图最多 64 条；性能需按实际多航迹数据验证。
- 时间序列子图只保留最后一份 LOD，等待后台结果时用旧 LOD 投影；尚无多级缓存和按字节计量的内存预算。航迹另有空间简化索引，当前边界见航迹指南。
- 原始样本游标查询、索引化 Y 适应、几何提交仍在 GUI 线程；乱序时间极端窗口可能回退 O(N)。
- 图片导出尚未迁移（坐标轴与图例已迁移，悬停高亮待完善）。
- **重放、Python API、`.mldatx` 视图文件已遗弃**——老版 QCustomPlot 时代的功能，Qt Quick 版不打算重做。
- CSV 不支持跨行引号字段；数据整体驻留内存，非分块/流式架构。
- MAT 依赖仓库内与 LLVM-MinGW 17 兼容的 matio/HDF5/zlib 静态库。

建议下一阶段：多级 LOD 缓存与内存预算 → 图片导出与会话路径重定位 → 百万/千万点性能基准。
