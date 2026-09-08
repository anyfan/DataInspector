# DataInspector（Qt Quick 重构版）

DataInspector 是面向工程时间序列数据的高性能查看器。当前分支已从 QCustomPlot/QWidget 绘图区切换到 Qt 6.8.3 Qt Quick Scene Graph：曲线使用 GPU 三角带绘制，视图缩放时按屏幕宽度执行 min/max 保峰值降采样，缺失值自动断线。

本分支的目标是先建立稳定、可扩展的 Qt Quick 渲染内核，再逐步迁移旧版的视图管理、游标和导出功能。Python 暂不参与本次重构。

## 当前可用功能

- Qt Quick 主窗口和 Qt Quick Controls 界面，支持浅色/深色主题切换。
- CSV/TXT 时间序列后台加载；第一列为时间，其余列为信号。
- 左侧信号列表，勾选/取消勾选曲线；多信号独立颜色。
- 1×1、1×2、2×2 子图布局（当前各子图显示同一组已勾选信号）。
- 鼠标拖动平移、滚轮缩放、自动适应全部数据。
- 单垂直游标和实时 X 值显示基础能力。
- 1–8 px 线宽调节；线宽通过三角带几何实现，不依赖驱动的 `GL_LINE` 宽度。
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
- 当前解析器为简单逗号分隔，不处理带引号、字段内逗号等完整 RFC 4180 语法。

### MATLAB MAT（可选）

源码包含 MAT 读取路径，可识别名称为 `p1`、`p2` 等二维 `double` 变量：第一列是时间，其余列是信号，`pN_title`/`pN_title2` 用作标题。由于仓库中现有 matio/HDF5 静态库不是 LLVM-MinGW 17 ABI 构建，默认配置关闭 MAT：

```powershell
cmake -S . -B build_qt6 -G Ninja `
  -DENABLE_MAT=OFF
```

要启用 MAT，必须先使用 Qt 6.8.3 LLVM-MinGW 工具链重新构建兼容的 matio、HDF5 和 zlib，再配置 `-DENABLE_MAT=ON`。否则程序会明确提示当前构建未启用 MAT 支持。

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
  -DENABLE_MAT=OFF

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

1. 启动 `DataInspector.exe`，点击“打开”选择 CSV/TXT 文件。
2. 在左侧列表勾选需要显示的信号；“清空”移除当前数据。
3. 使用“1×1”“1×2”“2×2”切换子图数量。
4. 在绘图区拖动进行平移，滚轮以指针位置为中心缩放；点击“适应”恢复全量范围。
5. 调整“线宽”查看 1–8 px 曲线；勾选“游标”后移动鼠标显示 X 位置。
6. “主题”在浅色和深色界面之间切换。

## 性能设计

渲染路径位于 `src/quick/plotitem.cpp`：

- QML 线程只维护视图状态，Scene Graph 渲染线程提交 GPU 几何。
- 折线展开成屏幕空间三角带，避免 `GL_LINE` 在不同驱动上的宽度限制和虚影。
- 每个可见像素桶保留最小值和最大值，密集数据缩小时仍保留峰值。
- NaN 将数据切成独立线段，绝不跨越缺失区间连接。
- 曲线节点和材质在场景图中复用，平移、缩放和游标更新不再删除并重建整棵节点树。

## 当前限制与后续计划

以下功能仍在迁移中，README 不再将其描述为已完成：

- LOD 尚在 `updatePaintNode()` 中同步生成，尚未加入按信号/范围/屏幕宽度的数据缓存和后台生成。
- 多子图尚未拥有独立的信号集合；X 轴同步、独立 Y 轴范围和激活子图待实现。
- 游标暂为单游标基础版，尚无双游标、精确插值、每条曲线 Y 标签和 ΔT/ΔY 面板。
- 坐标轴刻度、图例、悬停高亮、图片导出尚未迁移。
- `.mldatx`、JSON 视图、重放和 Python API 暂未接入 Qt Quick 版本。
- CSV 仍为简单逗号解析；数据目前整体读入内存，不是分块/流式架构。
- MAT 需要兼容 LLVM-MinGW 的第三方库后才能打开。

建议下一阶段按以下顺序推进：后台 LOD 缓存 → 多子图独立状态和 X 轴同步 → 完整游标/坐标轴 → 导出与视图文件 → 真实百万/千万点性能基准。

## 验证边界

当前已在 Qt 6.8.3 LLVM-MinGW 17.0.6 环境完成 Release 编译，并通过 `QT_QPA_PLATFORM=offscreen` 启动冒烟检查。尚未替代真实 GPU 驱动上的 GUI 手工回归，也未完成百万/千万点帧率基准；部署到目标机器后应再次检查 Qt Quick 图形后端和显卡驱动行为。

## 项目结构

```text
qml/Main.qml              Qt Quick 主界面
src/quick/plotitem.*      Scene Graph GPU 曲线项、LOD 和交互
src/quick/appcontroller.* 数据加载、信号选择和子图绑定
src/quick/signalmodel.*   QML 信号列表模型
renderer_research.md      后端选型与性能设计调研
tools/deploy_qt6.ps1      Windows 部署脚本
```
