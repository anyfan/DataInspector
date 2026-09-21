# 应用图标重绘与 exe 图标嵌入

## 背景

`assets/DataInspector.rc` 原先引用的是上游遗留的 `icon/plotjuggler.ico`，且该 `.rc` 从未加入任何 CMake 目标，因此构建出的 `DataInspector.exe` 实际不带图标，资源管理器与任务栏只显示系统默认图标。

## 图标设计

图标依据本程序的核心特性绘制，源文件 `assets/icon/DataInspector.svg`（256×256 viewBox）：

- **深蓝圆角方片**：与 Qt Quick 深色主题一致，圆角半径 54，顶部高光渐变模拟玻璃质感。
- **青色主曲线**：带一根明显的尖峰，对应"按屏幕宽度执行 min/max 保峰值降采样"这一核心渲染特性 —— 缩小视图时尖峰不丢失。
- **橙色副曲线**：取自固定 14 色调色板的 `#d95319` 系，体现多信号叠绘。
- **黄色垂直游标**：贯穿全图并带读数圆点，对应垂直游标与原始样本读数功能；游标下方垫一层深色描边，保证压在曲线上时仍清晰。
- **稀疏网格与坐标轴**：横竖各两条网格线。刻意不画满，否则 16×16 会糊成一片。

线宽经过小尺寸校验：主曲线 16、副曲线 12、坐标轴 9、游标 10（均基于 256 viewBox），缩到 16×16 后各元素仍可区分。

## 生成的文件

- `assets/icon/DataInspector.svg` —— 矢量源文件，后续改色或改版从这里出发。
- `assets/icon/DataInspector.ico` —— 多分辨率图标，含 16/24/32/48/64/128/256 七个尺寸，全部 32 位 RGBA、PNG 压缩帧。由 SVG 经 cairosvg 渲染到 512×512 后按 Lanczos 逐级缩放生成，而非简单放大单一位图。

旧的 `assets/icon/plotjuggler.ico` 与 `plotjuggler.svg` 未删除，仍被 `assets/resources.qrc` 引用，本次不动。

## 接入方式

### `assets/DataInspector.rc`

```
IDI_ICON1 ICON "icon/DataInspector.ico"
```

资源名 `IDI_ICON1` 保持不变。Windows 取 ID 字典序最小的图标资源作为 exe 图标，Qt 的 Windows 平台插件同样以第一个图标资源作为默认窗口图标，因此 exe 图标、任务栏图标和窗口左上角图标一次到位，不需要在 C++ 里调用 `setWindowIcon()`。

### `CMakeLists.txt`

```cmake
set(DI_WINDOWS_RESOURCES)
if(WIN32)
    set(DI_WINDOWS_RESOURCES ${CMAKE_SOURCE_DIR}/assets/DataInspector.rc)
endif()

qt_add_executable(DataInspector
    src/quick/main.cpp
    src/quick/qmltypes.h
    ${DI_CONTROLLER_SOURCES}
    ${DI_WINDOWS_RESOURCES}
)
```

`.rc` 用 `if(WIN32)` 包裹，非 Windows 平台构建时为空列表，不影响跨平台配置。测试目标不需要图标，未加入。

## 验证

- `cmake --build --preset qt6-clang-debug` 出现 `Building RC object CMakeFiles/DataInspector.dir/assets/DataInspector.rc.obj`，确认资源脚本已参与编译并链入 exe。
- 通过 PowerShell `System.Drawing.Icon::ExtractAssociatedIcon` 从构建产物 `build_qt6-debug/bin/DataInspector.exe` 提取图标，成功取到 32×32 图标并导出 PNG。
- 将提取结果与设计稿同尺寸逐像素比对，平均通道差 1.03/255（仅缩放重采样误差），确认 exe 内嵌的就是本次设计的图标。
- `ctest` 9/9 通过，`get_diagnostics` 无错误告警。

## 未做的部分

- 未注册 `.csv` / `.mat` 等数据文件的文件关联图标；文件关联属打包环节，需安装程序写注册表，届时可复用同一个 `.ico`。
- 未替换 `assets/resources.qrc` 中界面内使用的 `plotjuggler.svg`，界面内图标与应用图标目前相互独立。
- 未提供浅色主题变体；当前图标自带深色底片，在浅色任务栏上同样可辨认。
