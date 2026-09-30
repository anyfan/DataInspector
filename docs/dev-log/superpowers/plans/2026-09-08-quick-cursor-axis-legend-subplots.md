# Qt Quick 游标、坐标轴、图例与多子图迁移 Implementation Plan

> **状态**：已完成（归档） · **当前文档**：[视图操作](../../../user-guide/view-operations.md)
> **给 AI 代理**：这是历史实施计划，checkbox 任务均已执行完毕；实现细节已随迭代演进，请以现行文档和源码为准。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Extend the Qt Quick viewer with synchronized double cursors, raw-point snapping, axes/grid, dynamic legends, and independent multi-subplot bindings while preserving CSV/MAT loading.

**Architecture:** Keep raw series and view state in `PlotItem`, expose value-type cursor/range signals, and render plot data plus overlays from the same transform. Keep signal ownership in `AppController`/`SignalModel`; represent per-plot visibility and shared-X policy in controller bindings, while QML delegates render controls and legends.

**Tech Stack:** C++17, Qt 6.8 Quick/Scene Graph, Qt Quick Controls 2, QML, CMake/Ninja.

**Spec:** `../specs/2026-09-08-quick-cursor-axis-legend-subplots-design.md`

## Global Constraints

- Do not use QCustomPlot types in the Qt Quick path.
- Cursor positions snap to raw samples and never interpolate.
- Double cursors are visible in every subplot; only the X-axis region displays signed ΔT.
- Preserve existing CSV/TXT/MAT loading and `SignalModel` row APIs.
- Completion requires a successful Qt 6.8.3 LLVM-MinGW Release or Debug build.

---

### Task 1: Add cursor state and raw-point sampling to PlotItem

**Files:**
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp`

**Interfaces:**
- Produces QML properties `cursorMode`, `cursorX1`, `cursorX2`, `cursorDeltaT` and invokables `setCursorMode(int)`, `setCursorX(double, int)`.
- Produces signals `cursorValuesChanged()` and `cursorDeltaTChanged()` while retaining `cursorChanged()` compatibility.

- [ ] **Step 1: Add cursor properties and raw-sample helpers**

Declare a `CursorMode` enum, two cursor positions, nearest-sample lookup, and value snapshot members. Keep all accesses under `m_dataMutex`; use NaN for missing/invalid samples.

- [ ] **Step 2: Implement mode and nearest raw sample behavior**

Initialize cursor 1/2 to 5%/95% of the current range when enabling. Clamp incoming cursor positions to the current X range. For each series choose the nearest finite raw point by X; do not interpolate. Update the value snapshot and emit cursor signals.

- [ ] **Step 3: Update pointer interaction and Scene Graph overlay**

Drag the nearest cursor line when cursor mode is enabled, draw one or two lines in the persistent `PlotRoot`, and leave cursor 2 visible in every attached `PlotItem`. Do not draw ΔT inside the plot rectangle.

- [ ] **Step 4: Build**

Run `cmake --build build_qt6-debug --parallel 4`.

### Task 2: Add axes and grid overlay

**Files:**
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp`
- Modify: `qml/Main.qml`

**Interfaces:**
- Produces `axisTicksChanged`/`rangeChanged` driven tick snapshots and visible axis/grid overlays.

- [ ] **Step 1: Implement 1/2/5 tick generation**

Generate 5–9 ticks per axis from the current range and viewport dimensions, format labels from tick spacing, and retain safe defaults for empty or degenerate ranges.

- [ ] **Step 2: Render axis/grid geometry and labels**

Add persistent Scene Graph nodes for horizontal/vertical grid lines and axes; expose tick labels to a small QML overlay anchored to each `PlotItem` so text remains native and theme-aware.

- [ ] **Step 3: Add X-axis ΔT label region**

When `cursorMode == DoubleCursor`, show only `ΔT = cursorX2 - cursorX1` in the bottom axis area. Hide it for single/no cursor.

- [ ] **Step 4: Build**

Run `cmake --build build_qt6-debug --parallel 4`.

### Task 3: Migrate legend data and interactions

**Files:**
- Modify: `src/quick/signalmodel.h`
- Modify: `src/quick/signalmodel.cpp`
- Modify: `src/quick/appcontroller.h`
- Modify: `src/quick/appcontroller.cpp`
- Modify: `qml/Main.qml`

**Interfaces:**
- `SignalModel` roles include stable color and display name.
- `AppController` exposes `legendMode`, `togglePlotSignal(int plotIndex, int signalRow)`, and `plotSignalRows(int plotIndex)`.

- [ ] **Step 1: Add stable color role**

Assign colors by logical signal row during load and expose them as `QColor`/string data for QML delegates.

- [ ] **Step 2: Add per-plot binding state**

Track signal rows per plot, preserve global selection, initialize each plot from enabled signals, and rebuild plot series after a local legend toggle.

- [ ] **Step 3: Render flow legend delegates**

Place a compact top legend inside each subplot with wrapping `Flow`; show color swatch and name, and route clicks to `togglePlotSignal` without changing raw data.

- [ ] **Step 4: Build**

Run `cmake --build build_qt6-debug --parallel 4`.

### Task 4: Complete multi-subplot synchronization

**Files:**
- Modify: `src/quick/appcontroller.h`
- Modify: `src/quick/appcontroller.cpp`
- Modify: `src/quick/plotitem.h`
- Modify: `src/quick/plotitem.cpp`
- Modify: `qml/Main.qml`

**Interfaces:**
- Each plot has independent Y fitting and local signal rows; all plots share X ranges by default.

- [ ] **Step 1: Preserve plot attachments across layout changes**

Stop clearing controller bindings in `setLayout`; let QML reattach items and apply the current binding state to new plots.

- [ ] **Step 2: Synchronize X range changes**

Connect `PlotItem::rangeChanged` once per attached plot, guard against recursive updates, and propagate only X bounds while retaining each plot’s Y bounds.

- [ ] **Step 3: Apply independent plot series and cursor mode**

Ensure every plot receives its own selected series, current cursor mode, and both cursor positions; fit Y independently after changes.

- [ ] **Step 4: Build**

Run `cmake --build build_qt6-debug --parallel 4`.

### Task 5: Final compilation verification

**Files:**
- Verify: `CMakeLists.txt`, `src/quick/*.cpp`, `src/quick/*.h`, `qml/Main.qml`

- [ ] **Step 1: Configure if needed**

Run `cmake --preset qt6-debug` if `build_qt6-debug/CMakeCache.txt` is absent.

- [ ] **Step 2: Build**

Run `cmake --build build_qt6-debug --parallel 4` and require exit code 0.

- [ ] **Step 3: Check diff**

Run `git diff --check`; report any inability to commit caused by the workspace’s `.git` permissions.
