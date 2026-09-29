# Maximized subplot X fitting

- Requirement: with a multi-subplot layout, maximizing one subplot must make "自适应时间轴" / "自适应视图" fit that subplot's own signal time span, not the union of every subplot.
- Cause: `AppController::fitPlots` always built the X union from `m_signals->plotRows(index)` across **all** subplots, and the maximize state lived only in QML (`window.subplotMaximized` in `qml/Main.qml`). The backend never knew a subplot was maximized, so hidden subplots kept stretching the shared X range.
- Backend: `src/quick/appcontroller.h` adds the `soloPlotIndex` property (`-1` while tiled) with `setSoloPlot(int)` and the `soloPlotChanged()` signal.
- Fit: `src/quick/appcontroller_plots.cpp` restricts the X union to `m_soloPlotIndex` when it is set, and also skips hidden subplots during Y fitting so "自适应全部 Y 轴" does not rescale plots the user cannot see. `setLayout` resets the solo index because the maximized slot may not survive a grid change.
- QML: `qml/Main.qml` mirrors `subplotMaximized` into `appController.setSoloPlot(...)`, and re-sends it on `activePlotChanged` while maximized so switching the maximized subplot re-targets the fit.
- X sync unchanged: `applySharedXRange` still keeps every subplot on one shared time axis, so restoring the tiled layout and fitting again returns to the union range.
- Regression: `tests/appcontroller_test.cpp` extends `fittingUsesUnionOfSubplotTimeRanges` with solo fits for plot 1 (98..202) and plot 0 (-0.2..10.2) plus a restore-to-union assertion.
- Validation: Release `cmake --build build_qt6-release` and `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
