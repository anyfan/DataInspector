# 工程代码整理

## 目标

清理长期未整理的工程结构，不改变功能行为；所有测试保持通过。

## 变更

### 移除与归档

- 删除未参与构建的旧版 QWidget/QCustomPlot 代码：`src/core`、`src/plot`、`src/ui`、
  `src/script`、`src/data`、`src/main.cpp`（29 个文件，约 6500 行）。历史可在 git 中查看。
- `docs/superpowers/`、`docs/refactoring-status.md`、`renderer_research.md` 移至 `docs/archive/`。

### CMake

- 抽出共用源列表（`DI_RENDER_SOURCES`、`DI_PLOTITEM_SOURCES`、`DI_SIGNAL_SOURCES`、
  `DI_XLSX_SOURCES`、`DI_CONTROLLER_SOURCES`、`DI_ICON_FILES`、`DI_QML_FILES`），
  应用与各测试目标不再各自重复列文件。
- 新增 `di_add_icon_resources()`、`di_add_test()` 函数和 `di_mat` 接口目标，
  MAT 的头文件与系统库只声明一次。
- `appcontroller_test` 额外把 `qml/*.qml` 打进 `qrc:/qt/qml/DataInspector/`，
  使从源码树加载的 `QuickPlot.qml` 能解析同模块的 `PlotLegend`/`PlotAxisArea`。

### AppController 拆分

| 文件 | 职责 |
|---|---|
| `appcontroller.cpp` | 构造/析构、工作线程、事件过滤、`clear()`、状态与进度 setter |
| `appcontroller_loading.cpp` | `loadFiles`/`removeFile`/`exportXlsx`/加载队列/`appendLoadedTables` |
| `appcontroller_plots.cpp` | 信号选择、图例动作、`attachPlot`/布局/自适应 |

抽出的私有辅助函数：`notifyPlotBindingsChanged()`（替代 9 处
`++m_plotStateRevision; emit plotBindingsChanged();`）、`updateCurrentFileLabel()`、
`applySharedXRange()`（替代 3 处同步循环）、`syncCursorsFrom()`、`unbindPlotSignals()`、
`plotAt()`、`localPathFrom()`/`isSupportedDataFile()`（匿名命名空间）。
头文件按属性/导入导出/信号/子图分组并加注释。

### QML 拆分

- `qml/PlotAxisArea.qml`：X/Y 轴留白区手势，原 `QuickPlot.qml` 中两段近乎相同的
  `MouseArea`（约 80 行）合并为一个带 `axis` 参数的组件。
- `qml/PlotLegend.qml`：图例 `Flow` 及其拖拽/菜单逻辑；对外暴露 `entryCount`。
- `QuickPlot.qml` 从 786 行减至约 600 行。

### C++ 格式

- `PlotItem` 的 `geometryChange`/`mousePressEvent`/`mouseReleaseEvent`/`hoverMoveEvent`/
  `wheelEvent` 及游标场景图节点构建，`DataLoadWorker` 的表头循环：由压缩单行展开为常规格式。

## 验证

- `cmake --preset qt6-clang-debug` 重新配置，Debug 全量构建通过。
- `ctest`：5/5 套件通过（含 QML 加载测试）。
- Release 构建通过。
- `git diff --check` 无告警。
