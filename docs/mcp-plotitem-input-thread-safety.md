# PlotItem input handler thread safety

- Context: `PlotItem` guards its view/cursor state with `m_dataMutex` because `setRange`, `setCursorX` and the LOD scheduler callback can run while input events are delivered (`AppController::applySharedXRange` pushes range updates into every other subplot from a `rangeChanged` handler).
- Issue: two input handlers read guarded members without holding the lock.
  - `mousePressEvent` snapshotted `m_xMinimum/m_xMaximum/m_yMinimum/m_yMaximum` into the drag-start fields after releasing the mutex, so a concurrent `setRange` could tear the snapshot and make the following pan jump.
  - `mouseMoveEvent` compared `m_cursorMode` unguarded while the very next line took the lock to read the cursor positions.
- Fix: `src/quick/plotitem.cpp` moves the drag-start snapshot inside the existing `QMutexLocker` scope in `mousePressEvent`, and `mouseMoveEvent` now calls the already-locked `cursorMode()` accessor instead of touching `m_cursorMode` directly.
- No behavior change is intended: the same values are read, only consistently.
- Validation: Release `cmake --build build_qt6-release` and `ctest --test-dir build_qt6-release --output-on-failure` passed 5/5 suites.
