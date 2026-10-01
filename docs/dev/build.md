# 构建

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联文件**：`CMakeLists.txt`、`CMakePresets.json`、`tools/deploy_qt6.ps1`、`assets/DataInspector.rc/.manifest`

## 工具链

- Windows 10/11
- Qt 6.8.3 `llvm-mingw_64`
- LLVM-MinGW 17.0.6（Clang）
- CMake 3.25+（`CMakePresets.json` 版本 6）
- Ninja

## CMake 预设

仓库提供两个预设：

- `qt6-clang-debug` → `build_qt6-debug/`
- `qt6-clang-release` → `build_qt6-release/`

```powershell
cmake --preset qt6-clang-debug
cmake --build --preset qt6-clang-debug
```

预设里已钉死 `CMAKE_RC_COMPILER` 到 `llvm-windres.exe`，避免 GNU windres 找不到配套 gcc（见 [工具链排错](toolchain-troubleshooting.md)）。

## 选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `ENABLE_MAT` | `ON` | MAT 读取/导出，依赖仓库内 matio/HDF5/zlib 静态库 |
| `ENABLE_GPU_TESTS` | `OFF` | 注册 GPU 光栅测试到 CTest，需要硬件 Scene Graph 后端 |

GPU 套件包括复用 `plotitem_raster_test --dense` 的 `plotitem_dense_raster_test`，检查密集预览边缘一致性。

`plotitem_compressed_raster_test` 追加 `--compressed`，覆盖多信号共用大范围 Y 轴的压缩显示。

## 关键 CMake 组织

`CMakeLists.txt` 把源文件拆成共用列表，避免应用和测试目标重复罗列：

- `DI_PLOTITEM_SOURCES` 统一包含 `plotitem.cpp`、`plotitem_cursor.cpp`、`plotitem_interaction.cpp`、`plotitem_render.cpp` 和 `trajectoryitem.*`；应用与绘图测试共用，使用 Qt 内置材质。
- 其他共用列表：`DI_RENDER_SOURCES`、`DI_SIGNAL_SOURCES`、`DI_XLSX_SOURCES`、`DI_MAT_SOURCES`、`DI_CONTROLLER_SOURCES`
- `DI_QML_FILES`、`DI_ICON_FILES`
- 函数：`di_add_icon_resources()`、`di_add_test()`
- MAT 通过 `di_mat` INTERFACE 目标统一暴露头文件和系统库

## 部署

```powershell
powershell -ExecutionPolicy Bypass -File tools/deploy_qt6.ps1 `
  -QtDir "D:/Software/Qt/6.8.3/llvm-mingw_64" `
  -BuildDir "build_qt6-release" `
  -OutputDir "dist/DataInspector"
```

脚本必须拷贝：

- `plugins/platforms`
- `plugins/imageformats`（含 `qsvg.dll`，否则工具栏 SVG 图标空白）
- `plugins/iconengines`（含 `qsvgicon.dll`）

## Windows 资源

- `assets/DataInspector.rc`：嵌入 exe 图标和应用清单。
- `assets/DataInspector.manifest`：`asInvoker`、Per-Monitor v2 DPI、UTF-8 代码页、supportedOS。
- 未嵌入清单时 Windows installer detection 可能把 exe 误判为安装程序而弹 UAC，进而导致资源管理器无法拖放文件进窗口（UIPI）。详见 [工具链排错](toolchain-troubleshooting.md)。

## 轨迹模块

轨迹核心 `render/trajectorybuilder.*` 纳入 DI_RENDER_SOURCES；`trajectoryitem.*` 纳入 DI_PLOTITEM_SOURCES；`appcontroller_trajectory.cpp` 和 qmltypes.h 纳入 DI_CONTROLLER_SOURCES；TrajectoryPlot.qml 纳入 DI_QML_FILES。没有新增 Qt 模块或第三方绘图库。

新增 trajectory_test（默认离屏）和 trajectory_raster_test（ENABLE_GPU_TESTS 时注册）。trajectory_test 用独立 DataInspectorTrajectoryTest QML 模块编译相同界面，产物在 trajectory_test_qml；同时测试源码与 qmlcachegen 版本，避免仅源码测试漏掉编译后绑定刷新问题。测试目标通过 qmltypes.h 中的 QML_FOREIGN 元数据注册新类型，须由 AUTOMOC 编译该头文件。
