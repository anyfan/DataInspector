# 快速上手

> **状态**：现行 · **读者**：最终用户 / 构建部署者 · **关联**：`docs/dev/build.md`

## 运行要求

- Windows 10/11
- Qt 6.8.3 `llvm-mingw_64`
- LLVM-MinGW 17.0.6（Clang）
- CMake 3.25 或更高（`CMakePresets.json` 版本 6）
- Ninja

仓库已提供预编译的 matio/HDF5/zlib 静态库（LLVM-MinGW 17 ABI），开箱即可启用 MAT 支持。

## 用预设构建

```powershell
cmake --preset qt6-clang-debug
cmake --build --preset qt6-clang-debug
```

Release 把 `debug` 换成 `release`。产物：

- Debug：`build_qt6-debug/bin/DataInspector.exe`
- Release：`build_qt6-release/bin/DataInspector.exe`

## 手动指定工具链（可选）

```powershell
$env:Path = "D:\Software\Qt\Tools\CMake_64\bin;D:\Software\Qt\Tools\Ninja;D:\Software\Qt\Tools\llvm-mingw1706_64\bin;$env:Path"

cmake -S . -B build_qt6 -G Ninja `
  -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.8.3/llvm-mingw_64" `
  -DCMAKE_CXX_COMPILER="D:/Software/Qt/Tools/llvm-mingw1706_64/bin/clang++.exe" `
  -DCMAKE_BUILD_TYPE=Release `
  -DENABLE_MAT=ON

cmake --build build_qt6 --parallel ([Environment]::ProcessorCount)
```

> 如果 `cmake --build` 时报 `windres: preprocessing failed.`，见 [工具链排错](../dev/toolchain-troubleshooting.md)。

## 部署为独立目录

构建完成后用脚本拷贝 Qt DLL、平台插件和 QML 模块：

```powershell
powershell -ExecutionPolicy Bypass -File tools/deploy_qt6.ps1 `
  -QtDir "D:/Software/Qt/6.8.3/llvm-mingw_64" `
  -BuildDir "build_qt6-release" `
  -OutputDir "dist/DataInspector"
```

部署包必须包含 `plugins/imageformats/qsvg.dll` 与 `plugins/iconengines/qsvgicon.dll`，否则工具栏 SVG 图标会空白——脚本已默认拷贝。

## 第一次加载数据

1. 启动 `DataInspector.exe`。
2. 点击"打开"选择一个或多个 CSV/TXT/XLSX/MAT 文件；或直接把文件拖进窗口。
3. 也可以把数据文件拖到 `DataInspector.exe` 图标上启动——程序启动后会自动导入；拖入 `.disession` 文件则直接恢复会话。
4. 在左侧信号树勾选信号到当前子图；拖动平移、滚轮缩放。
5. 通过"导出数据"菜单导出 XLSX 或 MAT。

支持的数据格式详见 [数据格式](data-formats.md)；视图操作与快捷键见 [视图操作](view-operations.md)。
