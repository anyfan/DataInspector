# DataInspector（Qt Quick 重构版）

DataInspector 是面向工程时间序列数据的高性能查看器。当前分支已从 QCustomPlot/QWidget 绘图区切换到 Qt 6.8.3 Qt Quick Scene Graph：曲线使用 GPU 三角带绘制，视图缩放时按屏幕宽度执行 min/max 保峰值降采样，缺失值自动断线。渲染内核已拆分为共享原始数据 Store、LOD 构建器和屏幕空间 Geometry 构建器，PlotItem 只负责视图交互和 Scene Graph 提交。

本分支的目标是先建立稳定、可扩展的 Qt Quick 渲染内核，再逐步迁移旧版的视图管理、游标和导出功能。Python 暂不参与本次重构。

## 当前可用功能

- Qt Quick 主窗口和 Qt Quick Controls 界面，支持浅色/深色主题切换。
- CSV/TXT/MAT 时间序列后台批量加载，支持文件选择器多选和从资源管理器拖放；第一列为时间，其余列为信号。
- 左侧信号树按“文件 → `pN` 数据表 → 信号”展开/折叠；信号行依次显示勾选状态、名称和线型预览。
- 1×1、1×2、2×1、2×2 快捷布局和 1–8 行、1–8 列自定义布局；改变布局会保留仍存在子图的信号绑定。
- 文件读取在后台串行执行并显示批量总进度；新增文件以增量方式加入序列仓库，不重建已加载曲线。
- 鼠标拖动平移、滚轮缩放、自动适应全部数据。
- 单/双垂直游标、原始样本读数和 ΔT 显示，子图间同步游标。
- 每个信号可独立设置颜色、1–20 px 线宽和实线/虚线/点线/点划线；图例同步显示画笔样式。
- NaN/无效样本分段绘制，避免跨缺失数据产生幽灵连接。
- 按可视范围和屏幕宽度生成 min/max LOD，缩小视图时保留尖峰和谷值。

## 数据格式

### CSV/TXT

```text
Time,Signal A,Signal B
0.0,1.2,4.5
0.1,1.4,4.1
```

- 第一行必须是表头，至少包含时间列和一个信号列。
- 每行列数应与表头一致；格式错误的行会被跳过。
- 时间无法转换的行会被跳过；信号无法转换时保存为 `NaN`，该位置在图上断线。
- 自动识别逗号、分号和制表符；支持单行内带引号的字段和双引号转义，暂不支持跨行引号字段。

### MATLAB MAT（可选）

源码包含 MAT 读取路径，可识别名称为 `p1`、`p2` 等二维 `double` 变量：第一列是时间，其余列是信号，`pN_title`/`pN_title2` 用作标题。当前仓库已提供 LLVM-MinGW 17 兼容静态库，默认配置开启 MAT：

```powershell
cmake -S . -B build_qt6 -G Ninja `
  -DENABLE_MAT=ON
```

如果替换或删除了仓库内的兼容库，可暂时使用 `-DENABLE_MAT=OFF` 构建 CSV/TXT 版本；启用 MAT 时必须确保 matio、HDF5 和 zlib 均使用同一 LLVM-MinGW 工具链构建。

## 构建环境

- Windows 10/11
- Qt 6.8.3 `llvm-mingw_64`
- LLVM-MinGW 17.0.6（Clang）
- CMake 3.21 或更高版本
- Ninja

确认工具链路径后执行：

```powershell
$env:Path="D:\Software\Qt\Tools\CMake_64\bin;D:\Software\Qt\Tools\Ninja;D:\Software\Qt\Tools\llvm-mingw1706_64\bin;$env:Path"

cmake -S . -B build_qt6 -G Ninja `
  -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.8.3/llvm-mingw_64" `
  -DCMAKE_CXX_COMPILER="D:/Software/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe" `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_MAT=ON

cmake --build build_qt6 --parallel 4
```

生成的程序：`build_qt6/bin/DataInspector.exe`。

也可以使用预置脚本部署 Qt DLL、平台插件和 Qt Quick QML 模块：

```powershell
powershell -ExecutionPolicy Bypass -File tools/deploy_qt6.ps1 `
  -QtDir "D:/Software/Qt/6.8.3/llvm-mingw_64" `
  -BuildDir "build_qt6" `
  -OutputDir "dist/DataInspector"
```

## 使用方法

1. 启动 `DataInspector.exe`，点击“打开”多选数据文件，或把 CSV/TXT/MAT 文件拖入窗口。
2. 先点击目标子图，再在左侧信号树展开文件和 `pN` 路径并勾选需要显示的信号；“清空”移除当前数据。
3. 使用快捷按钮切换常用布局，或通过“布局…”设置 1–8 行、1–8 列。
4. 在绘图区拖动进行平移，滚轮以指针位置为中心缩放；点击“适应”恢复全量范围。
5. 双击信号行右侧的线型预览可单独设置颜色、线宽和线型。
6. “主题”在浅色和深色界面之间切换。
7. 图例固定在顶部：左键定位信号树中的信号（自动清除搜索并展开父节点）；右键菜单从当前子图移除信号，保留加载数据和其他子图绑定。
8. “适应”/空格适应全局时间轴和活动子图 Y；“适应 X”仅适应全局时间轴，“适应 Y”按当前时间窗口适应活动子图，“全部 Y”适应所有子图。对应快捷键 Ctrl+Alt+T、Ctrl+Alt+Y、Ctrl+Shift+Y。
9. 鼠标位于 X/Y 轴刻度区域时，滚轮仅缩放对应轴。绘图区滚轮缩放双轴。
10. 游标线位于曲线上层；X/Y 读数支持点击切换紧凑/原始格式，Y 读数按每根游标避让。标签总高度超过绘图区时会裁剪。

## 渲染内核与性能设计

渲染路径位于 `src/quick/render/` 和 `src/quick/plotitem.cpp`：

- `PlotSeriesStore` 由 `AppController` 共享，持有不可变原始序列和数据代际号；子图只保存有序系列 ID，不复制原始数组。
- 游标查询始终访问原始样本，不使用 LOD、不插值；原始快照替换后，旧快照仍可安全读取。
- `PlotLodBuilder` 按视窗和屏幕桶生成结构化连续线段；每桶保留 min/max，NaN 分段，单调序列保留视窗两侧连接点。
- `PlotGeometryBuilder` 按每个信号的画笔属性将 LOD 线段转换成有限、裁剪后的屏幕空间三角带或三角形列表，不依赖 `GL_LINE` 宽度。
- `PlotItem` 只维护视图、交互和持久 QSG 节点；通过 `PlotLodScheduler` 在最多两个后台线程生成 LOD；每子图合并最新请求，取消旧任务并丢弃过期结果。Y 范围和尺寸变化复用 LOD，游标移动复用曲线几何。
- 曲线节点和材质在场景图中复用，平移、缩放和游标更新不再删除并重建整棵节点树。

## 当前限制与后续计划

以下功能仍在迁移中，README 不再将其描述为已完成：

- 当前每子图保留最后一份 LOD，等待后台结果期间使用旧 LOD 按新视窗投影。尚无多级缓存或按字节计量的内存预算。
- LOD 已移至后台；原始样本游标查询、Y 轴适应和几何提交仍在前台线程。
- 各子图维护独立信号集合和 Y 轴范围，X 轴范围保持同步；活动子图以蓝色边框标识。
- 游标支持单/双游标和原始样本读数、ΔT；尚无 ΔY 面板，查询按原始样本吸附，不插值。
- 图片导出尚未迁移；坐标轴与图例已迁移，悬停高亮仍待完善。
- `.mldatx`、JSON 视图、重放和 Python API 暂未接入 Qt Quick 版本。
- CSV 暂不支持跨行引号字段；数据目前整体读入内存，不是分块/流式架构。
- MAT 依赖仓库内与 LLVM-MinGW 17 兼容的 matio/HDF5/zlib 静态库。

建议下一阶段按以下顺序推进：多级 LOD 缓存和内存预算 → 导出与视图文件 → 重放与 Python API → 真实百万/千万点性能基准。

## 验证边界

当前已在 Qt 6.8.3 LLVM-MinGW 17.0.6 环境完成 Release 编译，并通过 `QT_QPA_PLATFORM=offscreen` 启动冒烟检查。尚未替代真实 GPU 驱动上的 GUI 手工回归，也未完成百万/千万点帧率基准；部署到目标机器后应再次检查 Qt Quick 图形后端和显卡驱动行为。

## 项目结构

```text
qml/Main.qml              Qt Quick 主界面
qml/QuickPlot.qml         单个子图的 QML 外壳、坐标轴和图例
src/quick/plotitem.*      Scene Graph GPU 曲线项、视图交互和节点提交
src/quick/render/*        原始序列 Store、LOD 构建器、异步调度器和几何构建器
src/quick/appcontroller.* 加载队列、会话状态、信号选择和子图绑定
src/quick/dataloadworker.* 后台 CSV/TXT/MAT 解析及进度通知
src/quick/loadedtable.h   加载结果值类型及跨线程元类型声明
src/quick/signalmodel.*   QML 信号列表模型
renderer_research.md      后端选型与性能设计调研
tools/deploy_qt6.ps1      Windows 部署脚本
```
