# Dense-curve zoom geometry

- Requirement: zooming or panning a dense series must not fill the plot with wedges or drop strokes. A pan must not be required to repair the picture.
- Cause: LOD samples were joined into one polyline and expanded as a triangle strip. While zoom/pan still shows the previous LOD in the new view, that strip filled its interior.
- Fix in `src/quick/render/plotgeometrybuilder.cpp`: clip each consecutive pair, emit a constant-width quad as a triangle list, split at non-finite or |pixel| >= 1e8 samples, and reject non-finite clip math. The earlier polyline-join and `roundedStroke` abort workarounds were removed.
- Regression: `tests/rendercore_test.cpp` and `tests/plotitem_lod_test.cpp`.
