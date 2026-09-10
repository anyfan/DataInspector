# DataInspector 渲染内核拆分设计

## 背景

当前 Qt Quick 绘图路径已经具备 GPU 三角带、按视窗生成 min/max LOD、NaN 断线、游标和多子图交互，但数据存储、LOD 算法、几何构建、视窗状态及 Scene Graph 提交仍集中在 `PlotItem`。这使算法只能通过 `QQuickItem` 间接测试，也让后续增加多级缓存或后台生成时容易把数据线程、GUI 线程和渲染线程耦合在一起。

本阶段只拆分渲染内核并建立清晰的数据接口。允许调整内部 C++ 接口及 QML 接线，但不引入后台 LOD 线程，不实现多级缓存，也不迁移图片导出、视图文件或重放功能。

## 目标

- 将原始序列存储、原始点查询和数据版本管理从 `PlotItem` 分离。
- 将视窗裁剪与 min/max LOD 变成不依赖 Qt Quick 的可测试算法。
- 将连续曲线段到屏幕空间三角带的转换变成独立、可测试的几何算法。
- 让 `PlotItem` 只管理视图与交互状态、调用渲染内核并提交 Scene Graph 节点。
- 用显式请求键控制同步的最后结果缓存，为后续多级缓存和后台生成提供稳定边界。
- 保持 CSV/MAT 加载、信号选择、游标、坐标轴、图例和多子图的用户功能可用。

## 非目标

- 不创建工作线程、线程池或异步 LOD 作业。
- 不实现跨视窗、跨分辨率的多级 LOD 缓存。
- 不修改 CSV/MAT 文件格式和加载语义。
- 不迁移图片导出、`.mldatx`、JSON 视图、重放或 Python API。
- 不引入 QCustomPlot 或新的绘图库。

## 架构

渲染路径拆分为四层：

```text
AppController
      |
      v
PlotSeriesStore  <---- cursor/raw sample queries
      |
      | SeriesSnapshot + LodRequest
      v
PlotLodBuilder
      |
      | LodResult (ordered continuous segments)
      v
PlotGeometryBuilder
      |
      | GeometryResult (triangle-strip vertices)
      v
PlotItem ----> persistent QSG nodes
```

### PlotSeriesStore

`PlotSeriesStore` owns the raw points and per-series metadata. A series contains an ID, color, points, monotonic-time flag and monotonically increasing data version. The store exposes read-only snapshots for LOD generation and raw-sample queries for cursors.

The store has no dependency on `QQuickItem`, QML or Scene Graph classes. It may use Qt value containers and types such as `QVector`, `QPointF` and `QColor` because the application already uses them throughout the data path.

Required behavior:

- Replacing, appending or clearing series increments a store generation.
- Each snapshot remains valid for the duration of a synchronous render-core call.
- Monotonicity describes finite X values in input order. Invalid X or decreasing finite X marks a series non-monotonic.
- Invalid Y remains represented as NaN and creates a line break.
- Cursor lookup uses original data, never LOD data and never interpolation.
- Monotonic finite-time data uses binary search; non-monotonic data uses a finite-point scan.
- Equal-distance cursor candidates resolve deterministically to the earlier point in original input order.

`AppController` owns one shared `PlotSeriesStore` for the loaded session and retains the signal-to-table mapping needed by the UI. Each `PlotItem` receives a shared read-only store handle and keeps only an ordered set of visible series IDs. Changing a subplot legend therefore changes IDs, not raw point containers, and all subplots refer to the same immutable series payloads.

Store mutation uses copy-on-write series payloads. A read snapshot contains shared immutable payload handles plus the captured generation, so it remains valid even if the GUI thread subsequently replaces or clears the active store contents. This avoids copying all points for every paint while preventing the render thread from observing a container during mutation.

### PlotLodBuilder

`PlotLodBuilder` is a stateless algorithm. Its request contains:

- store generation and requested series identity;
- X minimum and maximum;
- horizontal pixel bucket count;
- LOD mode parameters needed by the current min/max algorithm.

Its result contains the request key and an ordered list of continuous point segments. NaN separators are not required in the public result because segment boundaries are represented structurally.

For monotonic data, the builder uses binary search to locate the visible range and includes one finite neighboring point on each side when present. Those neighbors must survive reduction so a viewport entirely between adjacent samples still displays the crossing line. For non-monotonic data, it scans in original order and only admits finite points whose X lies in the viewport; invalid points terminate the current segment.

Each pixel bucket preserves extrema without changing temporal order. A bucket with one point emits it once. A bucket with distinct minimum and maximum emits them in ascending original sample-index order, not X or Y order. Repeated timestamps remain stable. Empty input, invalid ranges and non-positive bucket counts return an empty result.

The LOD builder does not project Y values, apply line width, clip screen coordinates or access Scene Graph objects.

### PlotGeometryBuilder

`PlotGeometryBuilder` converts LOD segments to screen-space triangle-strip vertices. Its request contains X/Y ranges, item width/height and line width. Its result contains one vertex buffer per drawable continuous segment.

Required behavior:

- Segments with fewer than two distinct drawable points do not produce a line buffer.
- Input points are projected using one explicit view transform shared by the geometry tests and `PlotItem`.
- Line width is expanded in screen space and does not rely on `glLineWidth`.
- Zero-length local tangents fall back to the nearest non-zero neighboring tangent; a fully degenerate segment is omitted.
- Generated coordinates are finite and clipped to the item bounds, matching the current clipped plot behavior.
- A NaN cannot cross a segment boundary because segmentation has already occurred in `PlotLodBuilder`.

Join style remains the current bounded normal expansion in this stage. Miter/bevel improvements are a separate visual change.

### PlotItem

`PlotItem` owns view ranges, cursor mode and positions, pointer interaction, tick snapshots, a shared read-only handle to `PlotSeriesStore`, one `PlotLodBuilder`, one `PlotGeometryBuilder`, an ordered visible-series ID set, and persistent Scene Graph nodes.

It performs these steps when painting:

1. Capture the current store generation, visible-series state, X range and item width into a `LodRequest`.
2. Reuse the last `LodResult` when its complete request key matches; otherwise rebuild synchronously.
3. Build geometry from the LOD result and current Y range, item height and line width.
4. Resize/refill existing QSG geometry buffers and clear unused nodes.

`PlotItem` must not scan raw series for LOD or construct triangle-strip normals itself after migration. Cursor lookup delegates to `PlotSeriesStore`. Axis tick generation and pointer range calculations remain in `PlotItem` for this stage because they are view/controller concerns rather than series rendering concerns.

## Cache And Invalidation

This stage keeps exactly one last LOD result per `PlotItem`. The cache key includes:

- store generation;
- ordered visible-series identities;
- X minimum and maximum using exact stored double values;
- horizontal bucket count;
- LOD algorithm mode/version.

LOD is rebuilt when any key field changes. It is reused when only the following change:

- Y minimum or maximum;
- item height;
- line width;
- cursor position or mode;
- axis ticks, legend mode or theme.

Width changes invalidate LOD only when they change the integer bucket count. Geometry is rebuilt for every paint request that follows a relevant visual change; persistent QSG nodes and allocated geometry objects remain reusable.

Data mutation cannot leave a stale cache entry because every store mutation changes the generation. No approximate floating-point cache comparison is used in this stage.

## Threading And Ownership

All store mutation and request construction remain on the GUI thread. `updatePaintNode()` executes on the render thread in Qt Quick's threaded render loop, so it consumes only the captured immutable store snapshot and copied view state; it must not read mutable GUI-thread QObjects directly.

The migration will therefore prepare an immutable value snapshot before Scene Graph submission. The render thread owns QSG nodes and geometry buffers after creation. No mutex is held while emitting Qt signals, and no signal is emitted from the render thread.

Snapshot publication remains inside `PlotItem` during this stage: GUI-side mutations replace a pending immutable snapshot under the existing synchronization boundary, and `updatePaintNode()` copies the shared handles before releasing that boundary. Core builders accept value objects and immutable payload handles and have no QObject ownership assumptions. This is the contract needed for a later background LOD worker.

## Error Handling

- Empty stores and empty visible-series sets render no curve nodes without changing the current view unexpectedly.
- Invalid or degenerate X/Y ranges produce no geometry and never divide by zero.
- NaN/Inf X breaks monotonic optimization and terminates the affected segment.
- NaN/Inf Y terminates a segment but remains in raw storage for cursor/data fidelity.
- Allocation or loading errors continue to be reported by the existing controller path; core algorithms return empty value results rather than user-visible messages.

## Migration Sequence

1. Add core request/result types and `PlotSeriesStore`, then migrate raw cursor lookup with behavior tests.
2. Add `PlotLodBuilder`, port the current algorithm, and cover boundary neighbors, extrema order, NaN, repeated timestamps, non-monotonic data and narrow viewports.
3. Add `PlotGeometryBuilder`, port projection and triangle-strip generation, and cover clipping and degenerate input.
4. Change `PlotItem` to orchestrate the three components and retain persistent QSG nodes.
5. Remove the old nested `Series`, `buildLod()` and in-place geometry-building code from `PlotItem`.
6. Update CMake, documentation and the existing Scene Graph integration test.

Each step is completed test-first. Production behavior is not removed until the replacement path passes its focused tests.

## Testing And Acceptance

Automated tests must cover:

- Store replacement, append/clear generation changes, monotonic detection and deterministic raw-point snapping.
- LOD empty/invalid input, one point, dense extrema preservation, repeated timestamps, NaN segmentation, non-monotonic input, and a viewport lying entirely between adjacent samples.
- LOD output preserves original temporal/sample order within every bucket.
- Geometry produces finite clipped vertices, honors line width, omits one-point/fully degenerate segments, and keeps separate segments separate.
- `PlotItem` integration still submits a crossing two-point line for an extremely narrow viewport.
- Existing cursor, range synchronization and QML-facing properties compile without warnings introduced by this change.

Completion requires:

- Qt 6.8.3 LLVM-MinGW Debug build succeeds.
- All CTest tests pass with zero failures.
- `git diff --check` reports no whitespace errors.
- The application starts with the offscreen platform and remains alive through the existing smoke-test window, if such a smoke command is available in the repository.

Manual GPU/GUI visual regression and million-point performance benchmarking are reported separately if not performed; they are not silently implied by build or offscreen success.

## Follow-Up Stage

After this split is stable, the next stage may replace the single-result cache with shared multi-level LOD entries and background generation. That work must preserve the request/result contracts defined here and add cancellation, stale-result rejection, memory limits and thread-safe snapshot ownership rather than moving QObject or QSG access into worker threads.
