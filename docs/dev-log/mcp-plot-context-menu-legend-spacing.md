# 子图右键菜单、图例行距与 Y 轴留白优化

## 背景

用户反馈（见截图）：

1. 图例换行后行间距过大。
2. Y 轴刻度文字与左边框之间有大量固定留白，需要按内容自适应缩小。
3. 需要在子图右键菜单中提供“清除当前子图所有信号”。
4. 设置菜单中的“清空数据”应改名为“清除所有信号”。
5. 需要在子图右键菜单中提供“自适应当前 Y 轴”。

## 改动

### `qml/QuickPlot.qml`

- 图例 `Flow.spacing` 从 3 改为 0，图例项高度改为 `Math.max(14, legendLabel.implicitHeight + 1)`，
  换行后行距仅由文字高度决定；每项宽度余量从 34 缩到 30。
- Y 轴留白改为自适应：新增 `widestYTickLabel`，遍历 `plotItem.yTicks` 找出当前最长刻度文本，
  `yTickMetrics` 以此测量宽度，`axisLeft = ceil(advanceWidth) + yTickLabelMargin(6) + 2`。
  原实现固定按 `"-9.99e-308"` 预留宽度，导致 `0 ~ 1` 这类刻度左侧大量空白。
  刻度 `Label` 的 `rightMargin/width` 同步使用 `yTickLabelMargin`，保证文字紧贴刻度线。
- 新增 `plotContextMenu`（`objectName: plotContextMenu`）与覆盖绘图区的右键 `MouseArea`
  （`plotContextArea`，仅接受 `Qt.RightButton`，z=4，不影响左键拖动/滚轮/游标），
  提供“自适应当前 Y 轴”“清除当前子图所有信号”两项；右键同时激活该子图。
- 图例项右键菜单 `legendMenu` 在“移除…”之后追加同样两项。
- 右键菜单“清除当前子图所有信号”始终可用（不再因子图为空而禁用）。
- 双游标 ΔT 徽标只显示数值，不再带 `ΔT = ` 前缀。

### `src/quick/plotitem.cpp`（游标）

- 游标由关闭切到开启时，初始位置改为可视 X 范围的 25% / 75%（原 5% / 95%），
  并通过 `nearestRawX` 吸附到最近的原始样本点。
- 双游标拖动互不穿越：拖左游标越过右游标时，左游标停在右游标位置并把拖动交给右游标，反之亦然
  （`mouseMoveEvent` 中在调用 `setCursorX` 前比较并交换 `m_cursorDragIndex`）。
- 新增测试 `draggingCursorPastTheOtherSwapsDrag` 覆盖上述交接行为。

### `src/quick/appcontroller.h` / `src/quick/appcontroller.cpp`

- 新增 `Q_INVOKABLE void clearPlotSignals(int plotIndex)`：解除该子图全部信号绑定，
  刷新子图并递增 `plotStateRevision`，不删除已加载数据。
- 新增 `Q_INVOKABLE void fitPlotY(int plotIndex)`：仅对指定子图调用 `PlotItem::fitY()`，
  不依赖当前激活子图。

### `qml/Main.qml`

- 设置菜单 `"清空数据"` 改为 `"清除所有信号"`，改为调用 `appController.clearAllPlotSignals()`：
  只解绑所有子图上已添加的信号，**不**移除已加载文件与数据（`clear()` 仍保留在后端供全清使用）。
- `plotPanel` 新增 `sharedAxisLeft`：收集所有可见 `QuickPlot` 的 `measuredAxisLeft` 取最大值，
  下发给每个子图，使所有子图的 Y 轴缩进一致、X 轴刻度逐列对齐。
  子图可见性、刻度文本变化、布局增删都会触发重新计算。

### `qml/QuickPlot.qml`（Y 轴缩进共享）

- `axisLeft` 拆为 `measuredAxisLeft`（自身所需宽度）与 `sharedAxisLeft`（父级下发），
  `axisLeft = max(measuredAxisLeft, sharedAxisLeft)`。

### `src/quick/appcontroller.*`（补充）

- 新增 `Q_INVOKABLE void clearAllPlotSignals()`：遍历全部子图解绑信号并刷新，保留数据仓库。

### `tests/appcontroller_test.cpp`

- `legendNavigationAndRemoval` 增加 `clearPlotSignals` 断言：清除后两条信号均不再绑定，
  `signalCount` 保持 2。

## 注意事项

- 单独实例化 `QuickPlot`（如测试）时 `sharedAxisLeft` 为 0，退化为自适应宽度；
  在 `Main.qml` 中由 `plotPanel` 统一对齐。
- 图例项最小高度 14 px 是为保持现有测试中 `QPoint(40, 12)` 的图例点击命中。

## 验证

- `cmake --build --preset qt6-clang-debug`：成功。
- `ctest`（build_qt6-debug）：5/5 通过（appcontroller_test 32 passed, 2 skipped）。
