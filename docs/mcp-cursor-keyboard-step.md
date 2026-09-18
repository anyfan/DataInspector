# Cursor keyboard stepping

- Requirement: when the cursor tool is enabled, Left/Right moves the cursor to the previous/next raw sample point.
- Lookup: `src/quick/plotitem.cpp` finds the adjacent sample X across the visible series snapshot; monotonic series use binary search, non-monotonic series keep the original scan, and duplicate timestamps are treated as one point.
- API: `src/quick/plotitem.h` exposes `PlotItem::stepCursor(direction, cursorIndex)`; `cursorIndex = 0` steps every cursor enabled by the current single/double-cursor mode, still clamped to the current X view like `setCursorX`.
- Focus: `qml/QuickPlot.qml` gives the active subplot key focus while its cursor mode is non-zero and binds Left/Right to `plotItem.stepCursor`, so focused editors outside the plot keep their normal arrow-key behavior.
- Regression: `tests/appcontroller_test.cpp` adds `cursorKeyboardStepsAcrossRawSamples` and a QML window Right-key assertion in `quickPlotLoadsWithLegendAndCursors`.
- Validation: Release `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
