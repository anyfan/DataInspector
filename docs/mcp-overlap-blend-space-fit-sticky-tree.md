# 重叠曲线混色 · 空格自适应 Y · 信号树粘性层级

## 1. 重叠曲线混色

- 问题：多条曲线重叠时只能看到最后绘制的一条。
- 实现：新增 `src/quick/plotblendmaterial.{h,cpp}`（`PlotBlendMaterial`），复用 Qt 内置 `flatcolor` qsb 着色器，仅通过 `updateGraphicsPipelineState` 修改混合方程：
  - `Darken`：`min(src, dst)`，浅色主题使用；单条曲线颜色不变，重叠处呈现两种颜色的暗混合。
  - `Lighten`：`max(src, dst)`，深色主题使用。
  - `Opaque`：与原 `QSGFlatColorMaterial` 等价。
  - min/max 幂等，同一条曲线的相邻三角形重叠不会改变自身颜色（alpha 混合会出现关节变深的问题）。设置 `Blending` 标志保证节点走保序的 alpha pass。
- `PlotItem` 新增 `blendMode` 属性（`OpaqueBlend/DarkenBlend/LightenBlend`），`curveView` 键包含该值以触发重新上传材质。
- `QuickPlot.qml` 新增 `blendOverlaps`（默认开启）按主题自动选择 Darken/Lighten；右键菜单“设置 → 重叠曲线混色”可关闭。
- 不需要 Qt ShaderTools（本机 Qt 未安装 qsb），不新增 shader 文件。

## 2. 空格键自适应当前子图 Y 轴

- 原行为：仅当鼠标悬停在 X/Y 轴刻度区时空格生效。
- 新行为：鼠标在绘图区（`PlotItem` 上的 `HoverHandler graphHover`）按空格 → `controller.fitPlotY(plotIndex)`，只作用于该子图；悬停轴区时保持原有单轴自适应。
- 焦点无关：`Shortcut` 为窗口级，`enabled` 由悬停状态控制。

## 3. 信号树滚动时顶部显示层级

- `SignalModel::ancestorPath(modelRow)`（`Q_INVOKABLE`）返回可见行的祖先组链 `{name, group, depth, row}`，`row` 为该组节点的可见行号。
- `Main.qml` 中 `signalList` 外包一层 `Item`，顶部叠加 `signalStickyHeader`：随 `contentY` 变化取顶行的祖先链，只显示已滚出视口的层级（文件 → pN），按深度缩进。
- 交互：单击回到该层级行；双击折叠该组。
- 测试：`tests/signalselection_test.cpp` `ancestorPathReportsVisibleGroupChain`。

### 3.1 快速滚动卡顿 / 空白修正

- 卡顿原因：每次 `contentY` 变化都同步调用 `ancestorPath`（其中对组节点行做线性扫描）并 `JSON.stringify` 比较；同时每个信号行的线型预览是 `Canvas`，flick 时大量 FBO 重绘。
- 空白原因：flick 中 `indexAt` 可能短暂返回 -1（顶边下尚无代理），原逻辑随即清空表头；`Canvas` 重绘排队时也会短暂空白。
- 修正：
  - `SignalModel` 增加 `m_groupRows` 哈希，`ancestorPath` 改为 O(depth) 查表。
  - QML 侧用 40 ms `Timer` 节流，仅当顶行索引变化才重新计算；`indexAt` 返回 -1 时保留上一次表头并稍后重试；`movementEnded`/`modelReset`/`countChanged` 时强制刷新。`cacheBuffer: 600` 预建代理。
  - 线型预览由 `Canvas` 改为 `QtQuick.Shapes`（scene graph 直接描边，dash 图案按 `strokeWidth` 换算），`qt_add_qml_module` 依赖增加 `QtQuick.Shapes`。
