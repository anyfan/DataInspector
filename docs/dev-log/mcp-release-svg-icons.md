# Release package SVG icons

- Symptom: after running `tools/deploy_qt6.ps1`, the packaged app starts but toolbar/QML icons are blank.
- Cause: the icons live in `qrc:/icons/*.svg`, but the package only copied `plugins/platforms`; Qt still needs `Qt6Svg.dll` plus the SVG decoder from `plugins/imageformats` (`qsvg.dll`). The script also now copies `plugins/iconengines` for completeness.
- Fix: deploy `plugins/imageformats` and `plugins/iconengines` alongside `platforms` in `tools/deploy_qt6.ps1`.
- Package verification: the release folder and zip must contain `imageformats/qsvg.dll` and `iconengines/qsvgicon.dll`.
