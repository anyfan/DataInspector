# DataInspector 绘图后端重构调研

日期：2026-09-07

> **状态**：决策留档（结论已落地为 Qt Quick Scene Graph） · **当前文档**：[渲染管线](../architecture/render-pipeline.md)
> **给 AI 代理**：本文件为后端选型的决策依据，供追溯；文中 `src/plot/*` 等旧路径已移除，勿据此定位代码。

## 结论先行

建议不要把 QCustomPlot 直接替换成另一个“同样的 QWidget 曲线控件”。当前遇到的幽灵线、缩放后锯齿和粗线卡顿，本质上是绘制路径、数据量和抗锯齿策略共同造成的；如果数据仍然以原始全量点直接交给新的 CPU 曲线控件，问题很可能只是换一种形式出现。

推荐路线已经确定为 Qt Quick Scene Graph 自定义 `QQuickItem`，并已在当前分支落地第一版：GPU 三角带曲线、屏幕像素 min/max LOD、NaN 分段、平移/缩放和基础游标。Qt Quick 同时承担 UI 外壳，避免 `QQuickWidget` 与 QWidget/FBO 的额外合成层。

后续重点不是继续更换绘图库，而是完善这条渲染管线：后台 LOD 缓存、节点/VBO 生命周期复用、多子图独立状态、坐标轴和游标信息面板。这样才能把“线条实、无幽灵线、粗线不卡”从原型能力提升为可测量的产品能力。

## 当前代码中的耦合点

当前实现不是只有一个 QCustomPlot 文件需要替换：

- `src/plot/plotmanager.cpp:97` 创建 `QCustomPlot`，并在 `:130` 配置拖拽、缩放、选择、图例和 OpenGL。
- `src/plot/plotmanager.cpp:337` 直接调用 `QCPGraph::setData`，把完整时间和值数组交给 QCustomPlot。
- `src/plot/plotmanager.h` 对外暴露 `QCustomPlot*`、`QCPGraph*` 和 `QCPRange`。
- `src/plot/cursormanager.*` 依赖 `QCPItemLine`、`QCPItemTracer`、`QCPItemText` 和 `QCPGraph`。
- `src/plot/replaymanager.*` 使用 `QCPRange` 表示时间范围。
- `src/core/mainwindow.*` 的信号槽、视图导出和信号选择都包含 QCustomPlot 类型。
- `CMakeLists.txt` 直接编译并链接 `third_libs/qcustomplot-2.1.1`。

因此，建议先定义自己的类型，例如 `PlotCanvas`、`PlotViewState`、`SeriesId`、`SeriesSnapshot` 和 `CursorState`，再切换后端。不要让新的后端继续把第三方绘图库类型泄漏到 MainWindow 和业务层。

## 候选方案比较

| 方案 | 线条/密集数据性能 | 重构工作量 | 与现有 Qt Widgets 兼容性 | 对当前问题的确定性 | 结论 |
| --- | --- | --- | --- | --- | --- |
| Qwt | 中等；仍以 QPainter/CPU 绘制为主 | 低到中 | 好 | 中等 | 适合快速替换和中等数据量，不适合作为根治方案 |
| Qt Charts | 中等或偏低；大数据和粗线仍受 CPU/场景图开销影响 | 中 | 好 | 低 | 不建议作为性能重构目标 |
| Dear ImGui + ImPlot | 高；可利用 GPU 后端 | 高；需解决 Qt 输入、停靠和样式整合 | 差 | 中等 | 原型可用，产品化整合成本高 |
| Qt Quick Scene Graph 自定义 `QQuickItem` | 高；GPU 几何和批处理可控 | 高 | 可通过 `QQuickWidget` 过渡，但长期更适合 Qt Quick 外壳 | 高 | 长期上限最高，适合整体 UI 迁移 |
| Qt Widgets + 自定义 `QOpenGLWidget` | 高；可完全控制 VBO、几何线宽和 LOD | 中到高 | 最好 | 高 | 当前项目首选 |
| Vulkan/Direct3D 自定义引擎 | 很高 | 很高 | 中等 | 高但风险大 | 当前需求过度设计 |
| 商业图表控件 | 取决于产品；可能有成熟 GPU LOD | 低到中 | 取决于 SDK | 需实测 | 预算允许时可评估，但必须先做数据和授权验证 |

### Qwt

Qwt 是最接近现有 QWidget 使用方式的替代品，坐标轴、曲线、图例和缩放交互都比较成熟，迁移成本明显低于自研 GPU 绘图。但它的主路径仍然是 QPainter，粗线、多个密集信号和频繁重绘仍会消耗 CPU。`QwtPlotDirectPainter` 可以减少部分重绘开销，但不能改变“每个原始点都参与绘制”的数据模型，也不能保证消除缩放级别变化造成的视觉伪影。

适用条件：信号数量少、每条信号点数在几十万量级以内、优先追求短期交付。当前用户描述的“提高线宽密集线条会卡”不满足这个条件，因此不推荐作为最终后端。

### Qt Charts

Qt Charts 与 Qt Widgets 集成方便，但它不是面向超大时间序列的专用 GPU 曲线引擎。即使使用 OpenGL 系列功能，也不能替代明确的 LOD、批量几何和数据分桶。它更适合业务图表，不适合本项目这种需要高频缩放、粗线和密集信号的检查器。

### Dear ImGui + ImPlot

ImPlot 的绘图路径轻量、交互原型快，适合做性能实验或内部工具。不过当前应用已有 QMainWindow、QDockWidget、QTreeView、Qt 菜单、Qt 文件对话框和 Qt 信号槽体系。把 ImGui 嵌进 Qt 后，需要处理输入焦点、DPI、字体、停靠布局、主题、截图和 Qt/GL 上下文生命周期。它也会让后续 Qt 原生 UI 维护变得复杂。

### Qt Quick Scene Graph

Qt Quick Scene Graph 允许通过自定义 `QQuickItem`/scene graph node 提交 GPU 几何，并使用 Qt 的渲染后端抽象。它适合实现：

- 每条信号独立的顶点缓冲和批量绘制。
- 粗线使用三角带/扩展折线几何，而不是依赖不可靠的 `GL_LINE` 宽度。
- 视图缩放时只更新可见窗口和 LOD 层级。
- 通过 Qt Quick 的动画和输入系统实现平移、缩放、游标和高 DPI。

注意：不要使用 `QQuickPaintedItem` 作为最终高性能绘图区；它通常会把绘制回退到 QPainter/纹理路径，无法发挥自定义 scene graph 的 GPU 优势。若选择 Qt Quick，建议让绘图区域成为真正的自定义 scene graph item，而不是把 QCustomPlot 包在 Quick 外面。

代价是 UI 外壳迁移较大。用 `QQuickWidget` 嵌进现有 QMainWindow 可以作为过渡，但会引入额外的纹理/FBO 合成和输入转发；长期若追求最高性能，建议最终迁移为 Qt Quick 主窗口。

### Qt Widgets + 自定义 QOpenGLWidget（备选）

这是保留旧版 QWidget 外壳时的可行备选方案，但已不是当前分支路线。它适合需要短期兼容旧菜单、停靠面板的场景；长期 UI 仍会受到 QWidget/QML 双栈维护和 FBO 合成复杂度影响。

推荐的绘制分工：

- `QOpenGLWidget`：只负责绘制曲线、游标线、选中状态和必要的网格。
- `QPainter` overlay：负责坐标轴刻度、标签、图例文字和导出文字质量。
- OpenGL VBO/VAO：每条信号使用可复用的 GPU 缓冲，避免每帧重新上传所有原始数据。
- 三角带或屏幕空间扩展线段：实现真实可控的 2px/4px/8px 线宽，不依赖驱动对 `GL_LINE` 的支持。

这能解决当前三个痛点：

1. “幽灵线”：不再依赖 QCustomPlot 自适应采样和 QPainter 路径连接；缺失值、桶边界和点顺序由自己的数据管线明确处理。
2. 缩小视图的锯齿：使用每像素桶的 min/max 包络或面积覆盖几何，缩放到不同层级时规则一致。
3. 粗线卡顿：只上传和绘制当前视窗的 LOD 数据；线宽增加只改变几何扩展，不把全量原始点重复送入 CPU 路径。

## 推荐的数据和渲染管线

```text
FileData/SignalTable
        │
        ├─ 原始时间和值（用于游标、导出和精确查询）
        │
        └─ LOD 缓存（按时间范围和屏幕宽度生成）
                    │
                    ▼
             PlotViewState
                    │
                    ▼
          PlotCanvas / GL renderer
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
     GPU 曲线/游标        QPainter 轴/文字
```

LOD 建议采用按屏幕像素的 min/max 包络：一个横向像素桶保存该桶内的最小值和最大值，并保留对应时间位置。必要时每桶再保存首尾点。它比简单抽点更适合检查器，因为孤立峰值不会被丢掉。

建议缓存键至少包含：`seriesId`、`xRange`、绘图区像素宽度、LOD 层级和数据版本号。视图缩放时优先复用已有层级，后台生成新层级，避免拖动过程中同步扫描全部数据。

## 必须明确的渲染细节

### 线宽

不要依赖 `glLineWidth`；在不同驱动和实现中可支持的宽度不同。将折线展开为屏幕空间三角带，并对尖角使用受限的 miter 或 bevel join，避免急转弯产生巨大尖刺。

### 抗锯齿

先以无抗锯齿、稳定的 1px/2px 几何验证数据正确性，再增加 MSAA 或 shader-based smoothing。抗锯齿不能用来修复错误的降采样或错误的折线连接。

### 缺失值和桶边界

`NaN`、无效样本和时间不单调必须断线，不能跨越无效区间连接。min/max 输出要按时间顺序稳定排序，否则缩放时容易看到跳线或幽灵线。

### 游标

游标不要再依赖 `QCPItemTracer`。对原始时间向量做二分查找，在相邻点之间插值；绘图区只渲染游标线和标签。这样游标精度与显示 LOD 解耦。

### 导出

屏幕显示和图片导出应共用同一套 `PlotScene`/绘制命令。导出到高分辨率图片时，重新按目标像素宽度生成 LOD，不能直接放大屏幕缓存。

## 建议的重构边界

### 保留

- `DataManager` 的 CSV/MAT 加载和 `FileData`/`SignalTable` 数据结构。
- `SignalBrowser` 的搜索、勾选、颜色和文件移除功能。
- `ViewLoader` 的 `.mldatx`/JSON 解析格式。
- `ReplayManager` 的播放状态，但把 `QCPRange` 换成自有 `TimeRange`。
- Python API 的信号 ID、数据读取和视图控制语义。

### 重写

- `PlotManager`：改为只管理多个 `PlotCanvas` 和 `PlotViewState`。
- `CursorManager`：改为与第三方绘图库无关的坐标和游标状态管理。
- `mainwindow.h/.cpp` 中所有 `QCustomPlot*`、`QCPGraph*`、`QCPRange` 依赖。
- 视图导出、信号拾取、图例和右键菜单中对 QCustomPlot 对象的直接访问。
- CMake 中 QCustomPlot 静态库和 `QCUSTOMPLOT_USE_OPENGL` 定义。

建议的新接口大致如下：

```cpp
struct TimeRange { double lower; double upper; };

struct PlotViewState {
    TimeRange xRange;
    double yMin;
    double yMax;
    bool xAuto;
    bool yAuto;
};

class PlotCanvas : public QWidget {
public:
    virtual void setSeries(const QList<SeriesSnapshot>&) = 0;
    virtual void setViewState(const PlotViewState&) = 0;
    virtual PlotViewState viewState() const = 0;
    virtual void setCursorState(const CursorState&) = 0;
    virtual QImage renderToImage(const QSize&) = 0;
};
```

实际实现可以让 `PlotCanvas` 继承 `QOpenGLWidget`；业务层只看到上述接口，不再看到 QCP 类型。

## 分阶段实施计划

### 阶段 0：建立验收基线

准备至少三组固定数据：

- 高密度正弦/锯齿/脉冲数据，用于验证峰值和缩放。
- 含 NaN、重复时间和不均匀采样的数据，用于验证断线和游标。
- 多信号、多线宽、多子图压力数据，用于验证拖动时延。

记录不同缩放级别下的截图、帧时间、CPU、内存和首帧延迟。验收重点是：不出现幽灵连接、峰值不丢失、4px/8px 线宽拖动仍可用。

### 阶段 1：抽象后端

先保留 QCustomPlot，实现 `PlotCanvas`/`PlotViewState` 接口，让 MainWindow、CursorManager 和 ReplayManager 不再新增 QCP 依赖。此阶段只做接口解耦，不追求性能变化。

### 阶段 2：实现 QOpenGLWidget 后端

先完成单子图：坐标变换、LOD、稳定折线、线宽、平移/缩放和截图。再接入多子图、图例、游标和右键菜单。

### 阶段 3：替换交互和导出

将现有拖放、信号选择、适配视图、重放和 JSON 视图恢复接到后端无关接口。完成后移除 QCustomPlot 和所有 QCP 类型。

### 阶段 4：性能优化

增加 LOD 多级缓存、后台生成、VBO 复用、脏区域更新和高分辨率离屏导出。只有在这些数据路径完成后，才评估是否需要 Qt Quick Scene Graph。

## 最终建议

当前分支采用 **Qt Quick Scene Graph 自定义 QQuickItem + 三角带几何 + min/max LOD**。这是性能优先、同时能实现 SDI 风格高级交互的长期方案。Qwt、Qt Charts 和 ImPlot 可用于对照实验或临时原型，但不作为主线后端。

不建议直接换 Qt Charts，也不建议为了性能把整个应用改成 ImGui，除非愿意同时接受 UI 框架和交互体系的重写。

## 参考资料

以下为应在实现前核对的官方/项目资料入口；当前环境无法稳定建立外部 HTTPS 连接，本文没有把网络资料当作本项目的实测结论：

- Qt Quick Scene Graph：<https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html>
- Qt Quick 自定义绘制项：<https://doc.qt.io/qt-6/qquickitem.html>
- Qwt：<https://qwt.sourceforge.io/>
- ImPlot：<https://github.com/epezent/implot>
- QCustomPlot 大数据示例：<https://www.qcustomplot.com/index.php/demos/largeDataDemo>

