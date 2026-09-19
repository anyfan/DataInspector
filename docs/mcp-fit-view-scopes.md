# Fit-view menu scopes

- Requirement: split the single "自适应视图" entry into explicit current/all scopes for the whole view, the time axis and the Y axis.
- Menu: `qml/Main.qml` `fitMenu` now lists 自适应当前视图 / 自适应全部视图, 自适应当前时间轴 / 自适应全部时间轴, 自适应当前 Y 轴 / 自适应全部 Y 轴, grouped by `MenuSeparator`.
- Shortcuts: `Ctrl+Alt+F` 视图, `Ctrl+Alt+T` 时间轴, `Ctrl+Alt+Y` Y 轴; adding `Shift` switches the same action from the current subplot to all subplots.
- Backend: `AppController::fitPlots(fitX, fitY, allPlots)` keeps its signature; `allPlots` now also selects the X source. `src/quick/appcontroller_plots.cpp` extracts two helpers declared in `src/quick/appcontroller.h`:
  - `fitScopeIncludes(plotIndex, allPlots)` — single place deciding which subplots a fit applies to. A maximized subplot (`soloPlotIndex >= 0`) always wins over an "all" request; otherwise "current" means `activePlotIndex()`.
  - `fitSourceRows(allPlots)` — the signal rows whose `timeBounds` define the fitted X range, built through the same scope rule.
- Shared axis: X fitting still calls `applySharedXRange`, so every subplot keeps one common time axis. Scope only changes which signals define the range, not which subplots receive it.
- Regression: `tests/appcontroller_test.cpp` `fittingUsesUnionOfSubplotTimeRanges` asserts current-scope X fitting per active subplot (-0.2..10.2 and 98..202), the all-scope union (-4..204) and the existing maximized-subplot cases.
- Validation: Release `cmake --build build_qt6-release` and `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
