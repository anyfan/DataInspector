# DataInspector Render Core Separation Implementation Plan

> **状态**：已完成（归档） · **当前文档**：[渲染管线](../../../architecture/render-pipeline.md)
> **给 AI 代理**：历史实施计划，checkbox 任务均已执行完毕；后续已有异步 LOD 等演进，请以现行文档和源码为准。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split raw series storage, LOD reduction, and triangle-strip geometry generation out of `PlotItem` while preserving the current Qt Quick viewer behavior and narrow-viewport fix.

**Architecture:** Build a Qt value-type render-core library under `src/quick/render/`. `AppController` owns one shared `PlotSeriesStore`; each `PlotItem` publishes an immutable snapshot for its ordered visible IDs, resolves a synchronous last-result LOD cache, converts LOD segments to geometry, and only submits the resulting buffers to persistent QSG nodes.

**Tech Stack:** C++17, Qt 6.8.3 Core/Gui/Quick/Test, Qt Quick Scene Graph, CMake, Ninja, CTest, LLVM-MinGW 17.0.6.

**Spec:** `../specs/2026-09-10-render-core-separation-design.md`

## Global Constraints

- Do not introduce a worker thread, thread pool, asynchronous LOD job, or multi-level cache in this stage.
- Do not introduce QCustomPlot or another plotting library into the Qt Quick path.
- Preserve original points, NaN values, repeated timestamps, and input order; cursor lookup never uses LOD data and never interpolates.
- `AppController` owns one shared store; subplots hold ordered series IDs and immutable snapshots, not duplicated raw arrays.
- `updatePaintNode()` consumes copied view state and immutable payload handles only; it does not access mutable GUI-thread QObjects.
- Keep CSV/MAT loading, signal selection, axes, legends, cursors, and synchronized subplot interactions working.
- Completion requires a Qt 6.8.3 LLVM-MinGW Debug build, all CTest tests passing, and `git diff --check` clean.
- Preserve the existing uncommitted narrow-viewport regression fix in `src/quick/plotitem.cpp`, `tests/plotitem_lod_test.cpp`, and `CMakeLists.txt`; do not discard or overwrite it while restructuring tests.

## File Structure

- Create `src/quick/render/plotseriesstore.h`: series IDs, immutable payload/snapshot types, bounds and raw-sample query API.
- Create `src/quick/render/plotseriesstore.cpp`: store generation, copy-on-write replacement, monotonicity detection, bounds and deterministic cursor queries.
- Create `src/quick/render/plotlodbuilder.h`: `LodRequestKey`, structural LOD segments/results and synchronous last-result cache API.
- Create `src/quick/render/plotlodbuilder.cpp`: monotonic viewport search, boundary-neighbor retention, min/max reduction and NaN segmentation.
- Create `src/quick/render/plotgeometrybuilder.h`: view transform, geometry request and per-segment vertex-buffer result types.
- Create `src/quick/render/plotgeometrybuilder.cpp`: projection, tangent fallback, screen-space line expansion and clipping.
- Create `tests/rendercore_test.cpp`: Qt Test behavior suite for Store, LOD, cache and Geometry.
- Modify `src/quick/plotitem.h`: replace nested raw series/cache state with shared store, immutable snapshot, visible IDs and render-core objects.
- Modify `src/quick/plotitem.cpp`: delegate cursor lookup, LOD, cache and geometry work; retain range/tick/input/QSG ownership.
- Modify `src/quick/appcontroller.h`: own the shared store.
- Modify `src/quick/appcontroller.cpp`: populate the store once from transient loader tables, release controller-side raw arrays, and assign ordered visible IDs to plots.
- Modify `tests/plotitem_lod_test.cpp`: use the shared Store/visible-ID API while retaining the scene-graph regression assertion.
- Modify `CMakeLists.txt`: add `plot_render_core`, `rendercore_test`, and shared test-runtime setup.
- Modify `readme.md`: describe the separated synchronous core accurately and remove stale feature claims.

---

### Task 1: Preserve And Commit The Narrow-Viewport Regression Baseline

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/quick/plotitem.cpp:326-411`
- Test: `tests/plotitem_lod_test.cpp`

**Interfaces:**
- Consumes: Current `PlotItem::appendSeries(...)`, `setXRange(...)`, and protected `updatePaintNode(...)`.
- Produces: A committed regression baseline where a viewport between two adjacent samples submits a two-point line with four triangle-strip vertices.

- [ ] **Step 1: Verify the focused regression test still passes before further restructuring**

Run:

```powershell
cmake --preset qt6-clang-debug -DBUILD_TESTING=ON
cmake --build --preset qt6-clang-debug --target plotitem_lod_test
ctest --test-dir build_qt6-debug -R plotitem_lod_test --output-on-failure
```

Expected: `plotitem_lod_test` passes and reports `100% tests passed`.

- [ ] **Step 2: Verify the baseline build and whitespace state**

Run:

```powershell
cmake --build --preset qt6-clang-debug
git diff --check
```

Expected: the application links successfully; `git diff --check` returns exit code 0. CRLF conversion warnings are informational, not whitespace failures.

- [ ] **Step 3: Commit only the existing bug fix and its test harness**

```powershell
git add CMakeLists.txt src/quick/plotitem.cpp tests/plotitem_lod_test.cpp
git commit -m "fix: preserve lines across narrow plot views"
```

Expected: the design and plan commits remain separate; this commit contains no render-core files.

---

### Task 2: Add Immutable Shared Series Storage

**Files:**
- Create: `src/quick/render/plotseriesstore.h`
- Create: `src/quick/render/plotseriesstore.cpp`
- Create: `tests/rendercore_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Qt value types `QColor`, `QPointF`, `QVector`, `quint64`, and C++17 `std::shared_ptr`/`std::optional`.
- Produces: `PlotSeriesId`, `PlotSeriesInput`, `PlotSeriesData`, `PlotSeriesSnapshot`, `PlotSample`, `PlotBounds`, and `PlotSeriesStore`.

- [ ] **Step 1: Declare the Store contract and write failing generation/immutability tests**

Create `plotseriesstore.h` with this public API:

```cpp
#pragma once

#include <QColor>
#include <QPointF>
#include <QVector>

#include <memory>
#include <optional>

using PlotSeriesId = int;

struct PlotSeriesInput {
    PlotSeriesId id = -1;
    QVector<double> time;
    QVector<double> values;
    QColor color;
};

struct PlotSeriesData {
    PlotSeriesId id = -1;
    QColor color;
    QVector<QPointF> points;
    bool monotonicTime = true;
    quint64 version = 0;
};

using PlotSeriesDataPtr = std::shared_ptr<const PlotSeriesData>;

struct PlotSeriesSnapshot {
    quint64 generation = 0;
    QVector<PlotSeriesId> orderedIds;
    QVector<PlotSeriesDataPtr> series;
};

struct PlotSample {
    PlotSeriesId id = -1;
    double x = 0.0;
    double y = 0.0;
    QColor color;
};

struct PlotBounds {
    double xMinimum = 0.0;
    double xMaximum = 0.0;
    double yMinimum = 0.0;
    double yMaximum = 0.0;
};

class PlotSeriesStore final
{
public:
    void replaceSeries(const QVector<PlotSeriesInput> &inputs);
    void clear();
    quint64 generation() const;
    PlotSeriesSnapshot snapshot(const QVector<PlotSeriesId> &orderedIds) const;

    static std::optional<double> nearestX(const PlotSeriesSnapshot &snapshot,
                                          double targetX);
    static QVector<PlotSample> nearestSamples(const PlotSeriesSnapshot &snapshot,
                                              double targetX);
    static std::optional<PlotBounds> bounds(const PlotSeriesSnapshot &snapshot);

private:
    quint64 m_generation = 0;
    QVector<PlotSeriesDataPtr> m_series;
};
```

Create `tests/rendercore_test.cpp` with the Qt Test harness and the first test declaration:

```cpp
#include "render/plotseriesstore.h"

#include <QtTest>

class RenderCoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void storeGenerationAndSnapshotsAreImmutable();
};

QTEST_GUILESS_MAIN(RenderCoreTest)
#include "rendercore_test.moc"
```

Add the test body with hand-derived values:

```cpp
void RenderCoreTest::storeGenerationAndSnapshotsAreImmutable()
{
    PlotSeriesStore store;
    store.replaceSeries({{4, {0.0, 1.0}, {10.0, 11.0}, QColor("red")}});
    const PlotSeriesSnapshot oldSnapshot = store.snapshot({4});
    QCOMPARE(oldSnapshot.generation, quint64(1));
    QCOMPARE(oldSnapshot.series.at(0)->points.at(1), QPointF(1.0, 11.0));

    store.replaceSeries({{4, {0.0, 1.0}, {20.0, 21.0}, QColor("blue")}});
    QCOMPARE(store.generation(), quint64(2));
    QCOMPARE(oldSnapshot.series.at(0)->points.at(1), QPointF(1.0, 11.0));
    QCOMPARE(store.snapshot({4}).series.at(0)->points.at(1), QPointF(1.0, 21.0));

    store.clear();
    QCOMPARE(store.generation(), quint64(3));
    QVERIFY(store.snapshot({4}).series.isEmpty());
}
```

Keep the production package lookup unchanged, find Qt Test only when tests are enabled, and define the initial library/test wiring exactly once:

```cmake
add_library(plot_render_core STATIC
    src/quick/render/plotseriesstore.cpp
    src/quick/render/plotseriesstore.h
)
target_include_directories(plot_render_core PUBLIC src/quick)
target_link_libraries(plot_render_core PUBLIC Qt6::Core Qt6::Gui)

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(rendercore_test tests/rendercore_test.cpp)
    target_link_libraries(rendercore_test PRIVATE plot_render_core Qt6::Test)
    add_test(NAME rendercore_test
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:rendercore_test>
    )
endif()
```

Add `plot_render_core` to `DataInspector` and `plotitem_lod_test` link libraries. Later tasks append their `.cpp`/`.h` pairs to the existing `plot_render_core` source list instead of creating additional libraries.

- [ ] **Step 2: Run the Store test and verify RED**

Run:

```powershell
cmake --preset qt6-clang-debug -DBUILD_TESTING=ON
cmake --build --preset qt6-clang-debug --target rendercore_test
ctest --test-dir build_qt6-debug -R rendercore_test --output-on-failure
```

Expected: build/link or test failure because `PlotSeriesStore` methods are declared but not implemented.

- [ ] **Step 3: Implement replacement, generation, immutable payloads, and ordered snapshots**

In `plotseriesstore.cpp`, implement these exact rules:

```cpp
void PlotSeriesStore::replaceSeries(const QVector<PlotSeriesInput> &inputs)
{
    QVector<PlotSeriesDataPtr> replacement;
    replacement.reserve(inputs.size());
    for (const PlotSeriesInput &input : inputs) {
        auto data = std::make_shared<PlotSeriesData>();
        data->id = input.id;
        data->color = input.color.isValid() ? input.color : QColor("#4ea1ff");
        data->version = m_generation + 1;
        const int count = qMin(input.time.size(), input.values.size());
        data->points.reserve(count);
        double previous = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < count; ++i) {
            const double x = input.time.at(i);
            const double y = input.values.at(i);
            if (!qIsFinite(x)) {
                data->monotonicTime = false;
                data->points.append({qQNaN(), qQNaN()});
                continue;
            }
            if (x < previous)
                data->monotonicTime = false;
            previous = x;
            data->points.append({x, qIsFinite(y) ? y : qQNaN()});
        }
        replacement.append(std::move(data));
    }
    ++m_generation;
    m_series = std::move(replacement);
}
```

Implement `snapshot()` by resolving IDs in caller-provided order and skipping unknown IDs. `clear()` increments generation even if already empty, then clears active payload handles.

- [ ] **Step 4: Add failing tests for monotonicity, deterministic nearest samples, and bounds**

Add these independent cases:

```cpp
void RenderCoreTest::storeQueriesRawSamplesWithoutInterpolation()
{
    PlotSeriesStore store;
    store.replaceSeries({
        {1, {0.0, 2.0}, {10.0, 20.0}, QColor("red")},
        {2, {2.0, 0.0}, {40.0, 30.0}, QColor("blue")}
    });
    const PlotSeriesSnapshot snapshot = store.snapshot({2, 1});
    QCOMPARE(snapshot.orderedIds, QVector<PlotSeriesId>({2, 1}));
    QVERIFY(!snapshot.series.at(0)->monotonicTime);
    QVERIFY(snapshot.series.at(1)->monotonicTime);
    const auto nearest = PlotSeriesStore::nearestX(snapshot, 1.0);
    QVERIFY(nearest.has_value());
    QCOMPARE(*nearest, 2.0);

    const QVector<PlotSample> samples = PlotSeriesStore::nearestSamples(snapshot, 1.0);
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples.at(0).id, 2);
    QCOMPARE(samples.at(0).y, 40.0);
    QCOMPARE(samples.at(1).id, 1);
    QCOMPARE(samples.at(1).y, 10.0);

    const auto bounds = PlotSeriesStore::bounds(snapshot);
    QVERIFY(bounds.has_value());
    QCOMPARE(bounds->xMinimum, 0.0);
    QCOMPARE(bounds->xMaximum, 2.0);
    QCOMPARE(bounds->yMinimum, 10.0);
    QCOMPARE(bounds->yMaximum, 40.0);
}
```

The `nearestX` tie resolves by snapshot series order, then original point order. `nearestSamples` returns one nearest finite-Y sample per series in snapshot order. Bounds ignore all non-finite coordinates.

- [ ] **Step 5: Run RED, implement query helpers, then verify GREEN**

Run the focused test after adding declarations and before implementations; confirm failures are from missing/wrong query behavior. Implement binary search for monotonic series, linear scan for non-monotonic series, and strict `<` distance replacement so ties retain the earlier candidate.

Run:

```powershell
cmake --build --preset qt6-clang-debug --target rendercore_test
ctest --test-dir build_qt6-debug -R rendercore_test --output-on-failure
```

Expected: all Store cases pass.

- [ ] **Step 6: Commit the Store layer**

```powershell
git add CMakeLists.txt src/quick/render/plotseriesstore.h src/quick/render/plotseriesstore.cpp tests/rendercore_test.cpp
git commit -m "refactor: extract immutable plot series store"
```

---

### Task 3: Extract Structural LOD Reduction And Last-Result Cache

**Files:**
- Create: `src/quick/render/plotlodbuilder.h`
- Create: `src/quick/render/plotlodbuilder.cpp`
- Modify: `tests/rendercore_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PlotSeriesSnapshot`, `PlotSeriesData`, and ordered `PlotSeriesId` values from Task 2.
- Produces: `LodRequestKey`, `LodSegment`, `LodResult`, `PlotLodBuilder::build(...)`, and `PlotLodCache::resolve(...)`.

- [ ] **Step 1: Declare LOD value types and write the narrow-viewport RED test**

Create `plotlodbuilder.h`:

```cpp
#pragma once

#include "plotseriesstore.h"

struct LodRequestKey {
    quint64 storeGeneration = 0;
    QVector<PlotSeriesId> orderedIds;
    double xMinimum = 0.0;
    double xMaximum = 1.0;
    int bucketCount = 0;
    quint32 algorithmVersion = 1;

    bool operator==(const LodRequestKey &other) const;
};

struct LodSegment {
    PlotSeriesId seriesId = -1;
    QColor color;
    QVector<QPointF> points;
};

struct LodResult {
    LodRequestKey key;
    QVector<LodSegment> segments;
};

class PlotLodBuilder final
{
public:
    static LodResult build(const PlotSeriesSnapshot &snapshot,
                           const LodRequestKey &key);
};

class PlotLodCache final
{
public:
    const LodResult &resolve(const PlotSeriesSnapshot &snapshot,
                             const LodRequestKey &key);
    void clear();
    quint64 rebuildCount() const { return m_rebuildCount; }

private:
    std::optional<LodResult> m_result;
    quint64 m_rebuildCount = 0;
};
```

Implement `operator==` explicitly by comparing every field exactly. Add this test:

```cpp
void RenderCoreTest::lodKeepsBothNeighborsAcrossNarrowViewport()
{
    PlotSeriesStore store;
    store.replaceSeries({{7, {0.0, 1.0}, {0.0, 1.0}, QColor("red")}});
    const auto snapshot = store.snapshot({7});
    const LodRequestKey key{snapshot.generation, {7}, 0.4, 0.6, 800, 1};

    const LodResult result = PlotLodBuilder::build(snapshot, key);
    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.0, 0.0), QPointF(1.0, 1.0)}));
}
```

- [ ] **Step 2: Run the LOD test and verify RED**

Run:

```powershell
cmake --build --preset qt6-clang-debug --target rendercore_test
ctest --test-dir build_qt6-debug -R rendercore_test --output-on-failure
```

Expected: compile/link failure because `PlotLodBuilder::build` is not implemented.

- [ ] **Step 3: Port the minimal monotonic LOD path and make the narrow case GREEN**

Implement validation for finite ordered ranges and positive buckets. For monotonic data, use `lower_bound`/`upper_bound`, include one neighbor on each side, and do not apply a second in-viewport filter to those selected neighbors. Bucket indices are clamped to `[0, bucketCount - 1]`.

For each bucket, retain `(point, originalIndex)` pairs for minimum and maximum Y. Emit one point when they are equal; otherwise emit in ascending original index order.

Run the focused suite and expect the narrow test to pass.

- [ ] **Step 4: Add RED tests for extrema order, repeated time, NaN segmentation, and non-monotonic filtering**

Add these assertions as separate test slots:

```cpp
void RenderCoreTest::lodPreservesSampleOrderForBucketExtrema()
{
    PlotSeriesStore store;
    store.replaceSeries({{3, {0.0, 0.1, 0.2, 0.3},
                              {5.0, 9.0, 1.0, 6.0}, QColor("green")}});
    const auto snapshot = store.snapshot({3});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {3}, 0.0, 0.3, 1, 1});
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.1, 9.0), QPointF(0.2, 1.0)}));
}

void RenderCoreTest::lodKeepsRepeatedTimesAndSplitsAtNan()
{
    PlotSeriesStore store;
    store.replaceSeries({{5, {0.0, 0.0, 1.0, 2.0},
                              {1.0, 2.0, qQNaN(), 4.0}, QColor("cyan")}});
    const auto snapshot = store.snapshot({5});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {5}, 0.0, 2.0, 16, 1});
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.segments.at(0).points.size(), 2);
    QCOMPARE(result.segments.at(1).points, QVector<QPointF>({QPointF(2.0, 4.0)}));
}

void RenderCoreTest::lodFiltersNonMonotonicSeriesToViewportInInputOrder()
{
    PlotSeriesStore store;
    store.replaceSeries({{9, {2.0, 0.5, 1.5, -1.0},
                              {20.0, 5.0, 15.0, -10.0}, QColor("yellow")}});
    const auto snapshot = store.snapshot({9});
    const auto result = PlotLodBuilder::build(
        snapshot, {snapshot.generation, {9}, 0.0, 1.0, 10, 1});
    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).points,
             QVector<QPointF>({QPointF(0.5, 5.0)}));
}
```

Also assert empty results for an empty snapshot, `xMaximum <= xMinimum`, non-finite bounds, and `bucketCount <= 0`.

- [ ] **Step 5: Implement structural segmentation and all LOD edge behavior**

Use a per-series `currentSegment` and flush it on invalid X/Y. Preserve one-point segments in `LodResult`; the Geometry layer decides whether they are drawable. For non-monotonic series, scan in original order, skip finite out-of-range points without flushing, and flush on invalid points.

Run:

```powershell
cmake --build --preset qt6-clang-debug --target rendercore_test
ctest --test-dir build_qt6-debug -R rendercore_test --output-on-failure
```

Expected: all Store and LOD cases pass.

- [ ] **Step 6: Add RED tests for exact cache invalidation**

```cpp
void RenderCoreTest::lodCacheReusesOnlyAnExactRequestKey()
{
    PlotSeriesStore store;
    store.replaceSeries({{1, {0.0, 1.0}, {0.0, 1.0}, QColor("red")}});
    const auto snapshot = store.snapshot({1});
    PlotLodCache cache;
    LodRequestKey key{snapshot.generation, {1}, 0.0, 1.0, 100, 1};

    cache.resolve(snapshot, key);
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(1));

    key.bucketCount = 101;
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(2));
    cache.clear();
    cache.resolve(snapshot, key);
    QCOMPARE(cache.rebuildCount(), quint64(3));
}
```

Extend the test with one mutation each for generation, ordered IDs, X minimum, X maximum, and algorithm version; each mutation must increment `rebuildCount()` exactly once.

- [ ] **Step 7: Implement the last-result cache and verify GREEN**

`resolve()` compares the complete exact key. On mismatch it replaces `m_result` with `PlotLodBuilder::build(snapshot, key)` and increments `m_rebuildCount`. `clear()` resets only the optional result; it does not reset the lifetime diagnostic count.

Run all render-core tests and require zero failures.

- [ ] **Step 8: Commit the LOD layer**

```powershell
git add CMakeLists.txt src/quick/render/plotlodbuilder.h src/quick/render/plotlodbuilder.cpp tests/rendercore_test.cpp
git commit -m "refactor: extract plot LOD builder and cache"
```

---

### Task 4: Extract Screen-Space Geometry Generation

**Files:**
- Create: `src/quick/render/plotgeometrybuilder.h`
- Create: `src/quick/render/plotgeometrybuilder.cpp`
- Modify: `tests/rendercore_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `LodResult` and its structurally separate `LodSegment` values.
- Produces: `PlotViewTransform`, `GeometryRequest`, `GeometrySegment`, `GeometryResult`, and `PlotGeometryBuilder::build(...)`.

- [ ] **Step 1: Declare geometry value types and write the basic RED test**

Create `plotgeometrybuilder.h`:

```cpp
#pragma once

#include "plotlodbuilder.h"

struct PlotViewTransform {
    double xMinimum = 0.0;
    double xMaximum = 1.0;
    double yMinimum = -1.0;
    double yMaximum = 1.0;
    double width = 0.0;
    double height = 0.0;

    bool isValid() const;
    QPointF map(const QPointF &point) const;
};

struct GeometryRequest {
    PlotViewTransform transform;
    double lineWidth = 1.0;
};

struct GeometrySegment {
    PlotSeriesId seriesId = -1;
    QColor color;
    QVector<QPointF> vertices;
};

struct GeometryResult {
    QVector<GeometrySegment> segments;
};

class PlotGeometryBuilder final
{
public:
    static GeometryResult build(const LodResult &lod,
                                const GeometryRequest &request);
};
```

Add:

```cpp
void RenderCoreTest::geometryBuildsClippedTriangleStrip()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{-1.0, 0.0}, {2.0, 1.0}}});
    const GeometryRequest request{{0.0, 1.0, 0.0, 1.0, 100.0, 50.0}, 4.0};
    const GeometryResult result = PlotGeometryBuilder::build(lod, request);

    QCOMPARE(result.segments.size(), 1);
    QCOMPARE(result.segments.at(0).vertices.size(), 4);
    for (const QPointF &vertex : result.segments.at(0).vertices) {
        QVERIFY(qIsFinite(vertex.x()));
        QVERIFY(qIsFinite(vertex.y()));
        QVERIFY(vertex.x() >= 0.0 && vertex.x() <= 100.0);
        QVERIFY(vertex.y() >= 0.0 && vertex.y() <= 50.0);
    }
}
```

- [ ] **Step 2: Run the geometry test and verify RED**

Run the render-core target and CTest filter. Expected: missing implementation failure.

- [ ] **Step 3: Implement projection, normal expansion and clipping**

`isValid()` requires finite ranges/dimensions, strictly increasing ranges, and positive width/height. `map()` uses:

```cpp
return {
    (point.x() - xMinimum) / (xMaximum - xMinimum) * width,
    height - (point.y() - yMinimum) / (yMaximum - yMinimum) * height
};
```

For each point, find a non-zero tangent from adjacent projected points, normalize its perpendicular, expand by `max(1.0, lineWidth) / 2`, and clamp both generated vertices to `[0,width] x [0,height]`.

- [ ] **Step 4: Add RED tests for one-point, fully degenerate, and independent segments**

```cpp
void RenderCoreTest::geometryOmitsUndrawableSegments()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}}});
    lod.segments.append({2, QColor("blue"), {{0.5, 0.5}, {0.5, 0.5}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0});
    QVERIFY(result.segments.isEmpty());
}

void RenderCoreTest::geometryKeepsLodSegmentsIndependent()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}, {0.5, 0.5}}});
    lod.segments.append({1, QColor("red"), {{0.6, 0.6}, {1.0, 1.0}}});
    const auto result = PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0});
    QCOMPARE(result.segments.size(), 2);
    QCOMPARE(result.segments.at(0).vertices.size(), 4);
    QCOMPARE(result.segments.at(1).vertices.size(), 4);
}

void RenderCoreTest::geometryLineWidthChangesScreenSpaceExpansion()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.5}, {1.0, 0.5}}});
    const PlotViewTransform transform{0.0, 1.0, 0.0, 1.0, 100.0, 100.0};
    const auto thin = PlotGeometryBuilder::build(
        lod, GeometryRequest{transform, 2.0});
    const auto thick = PlotGeometryBuilder::build(
        lod, GeometryRequest{transform, 8.0});

    const double thinSpan = qAbs(thin.segments.at(0).vertices.at(0).y()
                                 - thin.segments.at(0).vertices.at(1).y());
    const double thickSpan = qAbs(thick.segments.at(0).vertices.at(0).y()
                                  - thick.segments.at(0).vertices.at(1).y());
    QCOMPARE(thinSpan, 2.0);
    QCOMPARE(thickSpan, 8.0);
}

void RenderCoreTest::geometryRejectsInvalidViewTransforms()
{
    LodResult lod;
    lod.segments.append({1, QColor("red"), {{0.0, 0.0}, {1.0, 1.0}}});
    QVERIFY(PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{1.0, 1.0, 0.0, 1.0,
                                               100.0, 100.0}, 2.0})
                .segments.isEmpty());
    QVERIFY(PlotGeometryBuilder::build(
        lod, GeometryRequest{PlotViewTransform{0.0, 1.0, 0.0, 1.0,
                                               0.0, 100.0}, 2.0})
                .segments.isEmpty());
}
```

- [ ] **Step 5: Implement tangent fallback and verify all geometry tests GREEN**

For each index, search outward for the nearest previous and next projected point differing by at least `1e-12` squared distance. Prefer the vector between both when available, otherwise current-to-next or previous-to-current. If no point in the segment has a usable tangent, omit the entire segment.

Run all render-core tests and require zero failures.

- [ ] **Step 6: Commit the Geometry layer**

```powershell
git add CMakeLists.txt src/quick/render/plotgeometrybuilder.h src/quick/render/plotgeometrybuilder.cpp tests/rendercore_test.cpp
git commit -m "refactor: extract plot geometry builder"
```

---

### Task 5: Migrate PlotItem And AppController To The Shared Store

**Files:**
- Modify: `src/quick/appcontroller.h`
- Modify: `src/quick/appcontroller.cpp:181-321`
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp:74-325`
- Modify: `tests/plotitem_lod_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: controller-owned `std::shared_ptr<PlotSeriesStore>`, PlotItem-owned `std::shared_ptr<const PlotSeriesStore>`, immutable snapshots, Store query helpers and the current `m_plotSignals` sets.
- Produces: `PlotItem::setSeriesStore(...)`, `PlotItem::setVisibleSeries(...)`, immutable pending snapshot state, and one shared controller-owned store.

- [ ] **Step 1: Update the PlotItem integration test to the desired Store API and verify RED**

Replace direct `appendSeries` setup with:

```cpp
auto store = std::make_shared<PlotSeriesStore>();
store->replaceSeries({{7, {0.0, 1.0}, {0.0, 1.0}, QColor("#4ea1ff")}});
plot.setSeriesStore(store);
plot.setVisibleSeries({7});
```

Keep the existing assertions that the narrow viewport produces exactly four vertices. Build the integration target.

Expected: compile failure because the two `PlotItem` methods do not exist.

- [ ] **Step 2: Add shared Store/snapshot members and setters to PlotItem**

Add:

```cpp
void setSeriesStore(const std::shared_ptr<const PlotSeriesStore> &store);
void setVisibleSeries(const QVector<PlotSeriesId> &orderedIds);

void refreshSnapshotLocked();

std::shared_ptr<const PlotSeriesStore> m_seriesStore;
QVector<PlotSeriesId> m_visibleSeries;
PlotSeriesSnapshot m_seriesSnapshot;
```

Both setters update members under `m_dataMutex`, refresh `m_seriesSnapshot` synchronously on the GUI thread, recalculate cursor readouts, then call `update()` after releasing the mutex. `setVisibleSeries` preserves caller order and ignores duplicate IDs after their first occurrence.

- [ ] **Step 3: Delegate PlotItem bounds and cursor queries to the immutable snapshot**

Replace `nearestRawX()` scans with `PlotSeriesStore::nearestX(m_seriesSnapshot, x)`. Replace per-series loops in `updateCursorValuesLocked()` with `nearestSamples()` for each cursor X. Build `cursorReadouts` from returned sample ID/color/Y values.

Replace raw loops in `fitView()` and `fitY()` with `PlotSeriesStore::bounds(snapshot)`. Preserve current fallback ranges and padding. For this stage `fitY()` continues using all finite Y values in the visible snapshot, matching current behavior rather than fitting only the visible X window.

- [ ] **Step 4: Populate one shared Store from AppController**

Add to `AppController`:

```cpp
std::shared_ptr<PlotSeriesStore> m_seriesStore;
```

Initialize it in the constructor. In `onLoadFinished()`, create one `PlotSeriesInput` per logical signal row directly from the `tables` argument, assigned logical row ID, color, table time and values, then call `m_seriesStore->replaceSeries(inputs)` once. Do not assign `tables` to `m_tables`; remove `m_tables` and `m_signalLocations` members after their remaining render call sites are migrated. `LoadedTable` remains the transient worker-to-controller transfer type.

In `attachPlot()`, call `item->setSeriesStore(m_seriesStore)` before refreshing IDs and use `!m_enabled.isEmpty()` as the loaded-data check. In `refreshPlot()`, replace `clearSeries()`/`appendSeries()` with a sorted vector built from `m_plotSignals[index]`, then call `plot->setVisibleSeries(ids)`. In `clear()`, call `m_seriesStore->clear()` once and set each attached plot's visible IDs to `{}`.

- [ ] **Step 5: Keep the old renderer temporarily reading the snapshot and remove duplicated PlotItem storage APIs**

Change the temporary private `buildLod` signature to accept `const PlotSeriesData &`; in `updatePaintNode()` iterate `m_seriesSnapshot.series`. Remove nested `Series`, `m_series`, `setSeries`, `appendSeries`, and `clearSeries` from `PlotItem` after all call sites are gone.

The temporary build remains behaviorally equivalent and still contains LOD/geometry code; Tasks 6 and 7 remove those responsibilities.

- [ ] **Step 6: Run integration, render-core, and full build verification**

```powershell
cmake --build --preset qt6-clang-debug
ctest --test-dir build_qt6-debug --output-on-failure
git diff --check
```

Expected: both `rendercore_test` and `plotitem_lod_test` pass; application links; no whitespace errors.

- [ ] **Step 7: Commit the shared Store integration**

```powershell
git add CMakeLists.txt src/quick/appcontroller.h src/quick/appcontroller.cpp src/quick/plotitem.h src/quick/plotitem.cpp tests/plotitem_lod_test.cpp
git commit -m "refactor: share raw series across plot items"
```

---

### Task 6: Replace PlotItem LOD With The Extracted Builder And Cache

**Files:**
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp:326-443`
- Modify: `tests/plotitem_lod_test.cpp`

**Interfaces:**
- Consumes: `PlotLodCache::resolve(snapshot, key)` from Task 3 and immutable `m_seriesSnapshot` from Task 5.
- Produces: `PlotItem` request-key construction and exact LOD invalidation without any in-item LOD algorithm.

- [ ] **Step 1: Add an integration assertion that Y/line-width changes retain drawable narrow-view geometry**

After the first four-vertex assertion, reuse the returned root and invoke:

```cpp
plot.setLineWidth(6.0);
plot.setXRange(0.4, 0.6);
root = plot.paint(root);
lineNode = dynamic_cast<QSGGeometryNode *>(root->firstChild());
QVERIFY(lineNode);
QCOMPARE(lineNode->geometry()->vertexCount(), 4);
```

Use the test executable's existing explicit error-return style if it has not migrated to Qt Test. The production mutation this guards against is clearing/rebuilding to an empty LOD result on a non-key visual change.

- [ ] **Step 2: Run the integration test before migration**

Expected: the new assertion passes on existing behavior, establishing equivalence rather than RED. The LOD builder itself already completed a strict red-green cycle in Task 3; this integration assertion protects wiring.

- [ ] **Step 3: Add cache state and construct exact request keys**

Replace per-series mutable cache fields with:

```cpp
PlotLodCache m_lodCache;
```

Inside `updatePaintNode()` while holding the existing synchronization lock, calculate:

```cpp
const int buckets = qBound(64, qCeil(width()), 4096);
const LodRequestKey lodKey{
    m_seriesSnapshot.generation,
    m_seriesSnapshot.orderedIds,
    m_xMinimum,
    m_xMaximum,
    buckets,
    1
};
const LodResult &lod = m_lodCache.resolve(m_seriesSnapshot, lodKey);
```

Do not put Y range, item height, line width, cursor state, ticks, legend or theme in this key.

- [ ] **Step 4: Replace in-item LOD scanning and remove `PlotItem::buildLod`**

Build the temporary `segments`/`segmentColors` arrays directly from `lod.segments`. Preserve one-point structural segments until the existing draw filter rejects fewer than two points. Remove `buildLod` declaration/definition and all old cache members.

- [ ] **Step 5: Verify focused and full tests**

```powershell
cmake --build --preset qt6-clang-debug
ctest --test-dir build_qt6-debug --output-on-failure
git diff --check
```

Expected: all tests pass; `rg -n "buildLod|cachedLod|cachedBuckets" src/quick/plotitem.*` returns no matches.

- [ ] **Step 6: Commit the PlotItem LOD migration**

```powershell
git add src/quick/plotitem.h src/quick/plotitem.cpp tests/plotitem_lod_test.cpp
git commit -m "refactor: delegate plot LOD generation"
```

---

### Task 7: Replace PlotItem Geometry Construction And Finalize Scene Graph Ownership

**Files:**
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp:11-72,413-501`
- Modify: `tests/plotitem_lod_test.cpp`

**Interfaces:**
- Consumes: `PlotGeometryBuilder::build(lod, request)` from Task 4.
- Produces: QSG submission from `GeometryResult` only; `PlotItem` no longer computes line normals, projections or clipped vertices.

- [ ] **Step 1: Strengthen the integration test to inspect finite clipped coordinates**

After obtaining the four geometry vertices, add:

```cpp
const auto *vertices = static_cast<const QSGGeometry::Point2D *>(
    lineNode->geometry()->vertexData());
for (int i = 0; i < lineNode->geometry()->vertexCount(); ++i) {
    if (!qIsFinite(vertices[i].x) || !qIsFinite(vertices[i].y)
        || vertices[i].x < 0.0f || vertices[i].x > plot.width()
        || vertices[i].y < 0.0f || vertices[i].y > plot.height()) {
        std::cerr << "Scene graph received an invalid or unclipped vertex\n";
        delete root;
        return 1;
    }
}
```

Run the test before migration and confirm it passes; Geometry behavior already had RED tests in Task 4.

- [ ] **Step 2: Make QSG upload accept final vertex buffers only**

Replace `updateLineNode(...)` with:

```cpp
static void uploadLineNode(QSGGeometryNode *node,
                           const GeometrySegment &segment)
{
    QSGGeometry *geometry = node->geometry();
    geometry->allocate(segment.vertices.size());
    auto *vertices = static_cast<QSGGeometry::Point2D *>(geometry->vertexData());
    for (int i = 0; i < segment.vertices.size(); ++i) {
        const QPointF point = segment.vertices.at(i);
        vertices[i].set(float(point.x()), float(point.y()));
    }
    geometry->markVertexDataDirty();
    static_cast<QSGFlatColorMaterial *>(node->material())->setColor(segment.color);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
}
```

This helper performs no projection, tangent, clipping or line-width calculations.

- [ ] **Step 3: Build GeometryRequest and submit GeometryResult**

After resolving LOD, call:

```cpp
const GeometryRequest geometryRequest{
    {m_xMinimum, m_xMaximum, m_yMinimum, m_yMaximum, width(), height()},
    m_lineWidth
};
const GeometryResult geometry = PlotGeometryBuilder::build(lod, geometryRequest);
```

Resize/reuse `PlotRoot::lineNodes` against `geometry.segments.size()`, upload active buffers with `uploadLineNode`, and allocate zero vertices for unused nodes. Cursor nodes remain owned and updated by `PlotItem` because they are interaction overlays, not series geometry.

- [ ] **Step 4: Remove all residual render-core algorithms from PlotItem**

Run:

```powershell
rg -n "lower_bound|upper_bound|currentBucket|bucketWidth|projectedA|projectedB|tangent|normal" src/quick/plotitem.cpp
```

Expected: no series LOD or line-geometry implementation remains. Cursor overlay projection may still contain direct scalar X projection and is allowed.

- [ ] **Step 5: Verify tests, application build, and offscreen startup**

```powershell
cmake --build --preset qt6-clang-debug
ctest --test-dir build_qt6-debug --output-on-failure
$env:Path = "D:\Software\Qt\6.8.3\llvm-mingw_64\bin;D:\Software\Qt\Tools\llvm-mingw1706_64\bin;" + $env:Path
$process = Start-Process -FilePath ".\build_qt6-debug\bin\DataInspector.exe" -ArgumentList "-platform", "offscreen" -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 3
if ($process.HasExited) { throw "DataInspector exited during offscreen smoke test with code $($process.ExitCode)" }
Stop-Process -Id $process.Id
git diff --check
```

Expected: build succeeds, all tests pass, application remains alive for three seconds, and whitespace check exits 0. The smoke process is intentionally terminated after the liveness check.

- [ ] **Step 6: Commit the Geometry integration**

```powershell
git add src/quick/plotitem.h src/quick/plotitem.cpp tests/plotitem_lod_test.cpp
git commit -m "refactor: delegate plot geometry generation"
```

---

### Task 8: Update Documentation And Perform Final Verification

**Files:**
- Modify: `readme.md:1-120`
- Verify: `CMakeLists.txt`
- Verify: `src/quick/render/*`
- Verify: `src/quick/plotitem.*`
- Verify: `src/quick/appcontroller.*`
- Verify: `tests/*`

**Interfaces:**
- Consumes: Completed Store, LOD, cache, Geometry and Scene Graph integration.
- Produces: Accurate project documentation and fresh completion evidence.

- [ ] **Step 1: Update README architecture and limitations**

Document these confirmed facts:

- shared immutable raw-series payloads live in `PlotSeriesStore`;
- cursor queries use original samples;
- `PlotLodBuilder` returns structural segments and preserves viewport neighbors/extrema;
- `PlotGeometryBuilder` produces clipped screen-space triangle strips;
- `PlotItem` owns interaction and QSG submission;
- the current cache retains only the last exact LOD request synchronously;
- background/multi-level LOD, image export, view files, replay and Python API remain future work.

Remove stale statements that axes, legends, double cursors or independent subplot bindings are still absent when the current implementation provides them. Do not claim manual GPU testing or performance benchmarks unless they are performed in this task.

- [ ] **Step 2: Run the complete verification suite fresh**

```powershell
cmake --preset qt6-clang-debug -DBUILD_TESTING=ON
cmake --build --preset qt6-clang-debug
ctest --test-dir build_qt6-debug --output-on-failure
git diff --check
git status --short
```

Expected: configuration and build exit 0; CTest reports both test executables passing with zero failures; whitespace check exits 0. Review `git status` and preserve any unrelated user changes.

- [ ] **Step 3: Inspect the responsibility boundaries**

```powershell
rg -n "lower_bound|upper_bound|currentBucket|bucketWidth|projectedA|projectedB" src/quick/plotitem.cpp
rg -n "QQuickItem|QSGNode|QSGGeometry" src/quick/render
```

Expected: first command has no series algorithm matches; second command has no matches, proving core files do not depend on Qt Quick or Scene Graph.

- [ ] **Step 4: Commit documentation and any final build wiring correction**

```powershell
git add readme.md CMakeLists.txt
git commit -m "docs: describe separated plot render core"
```

If `CMakeLists.txt` has no changes since earlier commits, stage and commit only `readme.md`.

- [ ] **Step 5: Review the complete branch diff**

```powershell
git log --oneline --decorate -8
git diff 4e0bac3..HEAD --stat
git status --short
```

Expected: the branch contains focused commits for the baseline fix, Store, LOD/cache, Geometry, integrations, and documentation; the working tree contains no task changes. Report any pre-existing unrelated changes explicitly instead of removing them.
