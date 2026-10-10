# 构建

对象派生实现 `appcontroller_objects.cpp`、`objectdefinition.*` 纳入 `DI_CONTROLLER_SOURCES`，`ObjectManager.qml`、`ThemedComboBox.qml` 纳入 `DI_QML_FILES`。后者为对象和航迹配置统一提供可读的下拉选中项。`objectdata_test` 链接现有控制器/绘图内核并包含同一 QML 资源；QML_FOREIGN 注册头也纳入测试 MOC。

## 关于页面的构建元数据

时间在生成元数据时保存为 UTC，在 `AppController::aboutInfo` 中转换为北京时间（固定 UTC+8）；页面与复制文本共用该显示值，不依赖电脑当前时区。

`di_build_info` 在每次构建 `di_app_core` 前执行 `cmake/GenerateBuildInfo.cmake`，生成构建目录内的 `generated/buildinfo.h`。版本来自 `PROJECT_VERSION`，UTC 时间、完整 Git hash、分支、工作区状态和 `$<CONFIG>` 在构建时采集，不需要手动重新 configure 来刷新提交信息。Git 不可用/源码包无 `.git` 时仍可构建并显示未知状态。

`DI_CONTROLLER_SOURCES` 包含 `appcontroller_about.cpp`，`DI_QML_FILES` 包含 `AboutDialog.qml`。仅关于信息实现包含生成头文件；更新记录从 `docs/user-guide/changelog.md` 嵌入该头文件，发布时无需额外复制文档。维护功能摘要时编辑此 Markdown，发布版本时修改 CMake 项目版本。

脚本比较源码/QML/资源/构建脚本/更新记录的 SHA256、版本、构建类型、编译器/编译选项、Qt/MAT 配置及 Git 元数据，将签名存入 `generated/buildinfo.signature`。输入不变时保留构建时间与头文件，避免无修改构建重新编译/链接；Git 检查仍运行，以便无需重新 configure 即可发现新提交或工作区状态变化。

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

Debug/Release 构建预设通过 Windows 的 `NUMBER_OF_PROCESSORS` 设置 `CMAKE_BUILD_PARALLEL_LEVEL`，自动使用本机逻辑 CPU 核数，不再固定为 4 个任务。使用 `cmake --build --preset qt6-clang-debug`（或 Release 预设）即可；需要手动限制时加 `--parallel 8` 等参数。直接指定构建目录而不使用预设时，需自行设置该环境变量或传入 `--parallel`。

## 选项

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `ENABLE_MAT` | `ON` | MAT 读取/导出，依赖仓库内 matio/HDF5/zlib 静态库 |
| `ENABLE_GPU_TESTS` | `OFF` | 注册 GPU 光栅测试到 CTest，需要硬件 Scene Graph 后端 |

GPU 套件包括复用 `plotitem_raster_test --dense` 的 `plotitem_dense_raster_test`，检查密集预览边缘一致性。

`plotitem_compressed_raster_test` 追加 `--compressed`，覆盖多信号共用大范围 Y 轴的压缩显示。

## 关键 CMake 组织

`CMakeLists.txt` 把源文件拆成共用列表，避免应用和测试目标重复罗列：

- `di_app_core` 静态库供应用及五个控制器测试目标共用，控制器/加载/导出源码只编译一次；`di_plot_items` 共用 PlotItem/TrajectoryItem，GPU 和 LOD 测试也复用它。两者用 `qt_extract_metatypes` 导出元类型，供 `QML_FOREIGN` 的注册与工具元数据使用；`qmltypes.h` 仍由各 QML 使用目标独立处理。
- `performance_benchmark` 在 `BUILD_TESTING=ON` 时构建，运行方式见 [测试](testing.md)。

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

## GitHub 自动构建与发布

工作流：`.github/workflows/windows-release.yml`。

- 推送 `main`：构建成功后更新 `latest` 标签对应的 **main 开发版预发布**，固定下载入口为 <https://github.com/anyfan/DataInspector/releases/tag/latest>。
- 推送 `v*` 标签（例如 `v0.1.0`）：发布同名正式 Release，发布说明来自随构建保存的 `docs/user-guide/changelog.md`。已存在的正式 Release 不覆盖。
- 指向 `main` 的 PR：只构建、检查和打包，不发布。
- Actions 页面可手动运行；仅 `main` 或 `v*` 标签运行允许发布。

使用标准 `windows-2022` runner，安装 Qt 6.8.3 `win64_llvm_mingw`、qtsvg 和 `tools_llvm_mingw1706`。CI 用 SDK 环境变量单独配置 `build_ci`，不依赖本机预设路径；`ENABLE_MAT=ON`，沿用仓库中的静态库。

CI 构建步骤使用 `[Environment]::ProcessorCount` 获取 runner 的逻辑 CPU 数，并传给 `cmake --build build_ci --parallel`，日志会打印实际并行任务数。该数值取决于 runner 提供的 CPU，独立于本机的 20 核和本地构建预设。

发布前依次完成 Release 应用/测试构建、`DataInspector_qmllint` 零 warning、Fusion/offscreen CTest 和部署包启动检查。GPU 测试关闭；离屏检查不等同真实 GPU 或人工 GUI 验收。

产物 `DataInspector-windows-x64.zip` 包含完整 `DataInspector/` 目录、运行依赖、文档和 `BUILD.txt`；`SHA256SUMS.txt` 可用于校验。必须解压整个目录再运行 exe。Actions 构建产物及测试报告保留 7 天，Release 下载包持续保留。

`tools/smoke_packaged_qt6.ps1` 临时清除进程中的 Qt/QML 搜索环境及 SDK PATH，检查平台/SVG/QML 文件，并启动包内应用等待 15 秒，捕获提前退出和 QML 加载错误；结束后恢复环境。它验证基础部署启动，不替代交互验收。

无需自行创建 PAT 或配置 Secrets：发布任务使用 GitHub 自动生成的 `GITHUB_TOKEN`，仅发布任务授予 `contents: write`。公开仓库的标准 GitHub-hosted runner 编译免费；不要切换到收费的 larger runner。

## Windows 资源

- `assets/DataInspector.rc`：嵌入 exe 图标和应用清单。
- `assets/DataInspector.manifest`：`asInvoker`、Per-Monitor v2 DPI、UTF-8 代码页、supportedOS。
- 未嵌入清单时 Windows installer detection 可能把 exe 误判为安装程序而弹 UAC，进而导致资源管理器无法拖放文件进窗口（UIPI）。详见 [工具链排错](toolchain-troubleshooting.md)。

## 轨迹模块

轨迹核心 `render/trajectorybuilder.*` 纳入 DI_RENDER_SOURCES；`trajectoryitem.*` 纳入 DI_PLOTITEM_SOURCES；`appcontroller_trajectory.cpp` 和 qmltypes.h 纳入 DI_CONTROLLER_SOURCES；TrajectoryPlot.qml 纳入 DI_QML_FILES。没有新增 Qt 模块或第三方绘图库。

新增 trajectory_test（默认离屏）和 trajectory_raster_test（ENABLE_GPU_TESTS 时注册）。trajectory_test 用独立 DataInspectorTrajectoryTest QML 模块编译相同界面，产物在 trajectory_test_qml；同时测试源码与 qmlcachegen 版本，避免仅源码测试漏掉编译后绑定刷新问题。测试目标通过 qmltypes.h 中的 QML_FOREIGN 元数据注册新类型，须由 AUTOMOC 编译该头文件。

多航迹/姿态继续使用 trajectorybuilder.*、trajectoryitem.*、appcontroller_trajectory.cpp 现有源列表；会话升级 v6，无新增绘图库、Qt 模块或模型加载依赖。

GPU 套件另注册 `plotitem_zoomed_raster_test` 和 `plotitem_zoomed_compressed_raster_test`，覆盖中间缩放的跨桶振荡。

GPU 套件注册 `plotitem_selected_dense_raster_test`，覆盖重复采样的毫秒锯齿波选中加粗显示。
