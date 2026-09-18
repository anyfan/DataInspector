# 归一化 Y 轴（Normalize y-axis）

## 需求
子图右键菜单增加「设置」子菜单，内含可勾选项「归一化 Y 轴 (Normalize y-axis)」。
勾选后各条曲线按各自的全量 min/max 缩放到 0..1 绘制，便于比较不同量级信号；
游标数值仍显示原始值。

## 实现
- `PlotItem` 新增 `Q_PROPERTY(bool normalizeY)`。
  - `rebuildNormalizationLocked()`：在快照刷新或切换开关时为每条可见序列计算
    `(min, span)` 并缓存到 `m_normalization`；常量序列绘制在 0.5。
  - `updatePaintNode()`：仅在 GUI 线程的 `LodResult` 副本上就地缩放各段点的 y，
    共享的 LOD 缓存仍是原始值；`curveView` 加入 normalizeY 标志以触发重建。
  - `fitY()`：归一化模式下固定 Y 范围为 -0.05..1.05。
  - `cursorReadouts` 每项新增 `displayY`（绘图坐标系下的 y），`y`/`text`/`rawText`
    保持原始值；QML 游标标签定位改用 `displayY`。
- `qml/QuickPlot.qml`：`plotContextMenu` 末尾新增 `Menu { title: "设置" }`，
  含 checkable `MenuItem` 绑定 `plotItem.normalizeY`。
- 测试：`AppControllerTest::normalizeYKeepsRawCursorValues`。

## 限制
- 归一化基于全量数据而非当前可视 X 范围。
- 开关为每个子图独立，不持久化。
