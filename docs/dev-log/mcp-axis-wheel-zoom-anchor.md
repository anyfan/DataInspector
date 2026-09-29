# Axis wheel zoom anchor

- Requirement: scrolling the wheel over an axis gutter should zoom around the pointer, keeping the data value under the cursor stationary (the usual plotting-tool behaviour).
- Previous behaviour: `PlotItem::zoomAxis(axis, fraction, steps)` accepted `fraction` but always anchored on the range midpoint, so `qml/PlotAxisArea.qml` passed the real pointer position and it was silently discarded.
- Fix: `src/quick/plotitem.cpp` anchors on `fraction`, clamped to 0..1. X uses `xmin + f * span` (left to right); Y uses `ymax - f * span` because the gutter fraction is measured top to bottom, matching `PlotAxisArea.fraction()` and `QuickPlot.applyAxisSelection`.
- Unchanged callers: `qml/Main.qml` keyboard zoom (`Ctrl++` / `Ctrl+-` / `Ctrl+Shift+T` / `Ctrl+Shift+Y`) passes `0.5`, which still anchors on the midpoint exactly as before.
- Regression: `tests/appcontroller_test.cpp` renames `axisZoomKeepsCurrentMidpoint` to `axisZoomAnchorsOnPointerPosition` and asserts the anchored data value is preserved for an off-centre X fraction (0.1) and Y fraction (0.9), that the span actually shrinks/grows, and that `fraction = 0.5` still reproduces midpoint zoom.
- Validation: Release `cmake --build build_qt6-release` and `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
