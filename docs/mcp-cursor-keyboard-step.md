# Cursor keyboard stepping

- Requirement: when the cursor tool is enabled, Left/Right moves the cursor to the previous/next raw sample point.
- Lookup: `src/quick/plotitem.cpp` finds the adjacent sample X across the visible series snapshot; monotonic series use binary search, non-monotonic series keep the original scan, and duplicate timestamps are treated as one point.
- API: `src/quick/plotitem.h` exposes `PlotItem::stepCursor(direction, cursorIndex)`; `cursorIndex = 0` steps every cursor enabled by the current single/double-cursor mode, still clamped to the current X view like `setCursorX`.
- Focus: `qml/QuickPlot.qml` gives the active subplot key focus while its cursor mode is non-zero and binds Left/Right to `plotItem.stepCursor`, so focused editors outside the plot keep their normal arrow-key behavior.
- Regression: `tests/appcontroller_test.cpp` adds `cursorKeyboardStepsAcrossRawSamples` and a QML window Right-key assertion in `quickPlotLoadsWithLegendAndCursors`.
- Validation: Release `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.

## 2026-09-19 fixes

### 1. Arrow keys dead after clearing signals

- Symptom: with the cursor tool on and signals loaded, clearing the signals made Left/Right stop moving the cursor.
- Cause A (focus): the clear actions are triggered from a `Menu` / legend button, which takes active focus away from the subplot. `qml/QuickPlot.qml` only declared `focus: ...`; that binding stays true but Qt never returns active focus on its own, so key events went nowhere.
- Cause A fix: `qml/QuickPlot.qml` adds `cursorKeysEnabled` plus `ensureCursorFocus()`, called via `Qt.callLater` on `cursorKeysEnabledChanged` and on the controller's `plotBindingsChanged` / `activePlotChanged`. It skips the reclaim when a text editor holds focus (detected through `activeFocusItem.selectedText`), so the signal search field keeps normal arrow-key behavior.
- Cause B (no samples): `PlotItem::stepCursor` returned early when `adjacentRawX` found no raw sample, which is always the case once a plot has no bound series.
- Cause B fix: `src/quick/plotitem.cpp` falls back to a 1% view-span step when the snapshot is empty, still clamped to the X view. Plots that do hold samples keep strict raw-sample snapping.

### 2. Cursor X-time label gap

- `qml/QuickPlot.qml`: the cursor time badges and the ΔT badge used `y: axisRect.y + axisRect.height + 3`, leaving a 3 px gap under the X axis. Both now use `y: axisRect.y + axisRect.height` so they sit flush on the axis.
- Regression: `tests/appcontroller_test.cpp` tightens the badge assertion from `label->y() > plotBottom` to `QCOMPARE(label->y(), plotBottom)`.
- Validation: Release `cmake --build build_qt6-release` plus `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
