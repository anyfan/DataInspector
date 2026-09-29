# 工具链排错

## `windres: preprocessing failed.`

**现象**：Debug 构建正常，Release 在 IDE 里编译 `assets/DataInspector.rc` 失败，手动终端跑同一条命令却成功。

**根因**：CMake 没指定 `CMAKE_RC_COMPILER`，从 PATH 自动探测。Debug 和 Release 构建目录可能探到不同结果：

- Debug：`llvm-windres.exe`（自带预处理）
- Release：GNU `windres.exe`（靠 `popen` 调用同目录 `gcc -E`）

VS Code 的 `cmake.environment` 改了 PATH，GNU windres 找不到配套 gcc，报错完全不提 gcc。

**修复**：`CMakePresets.json` 已钉死：

```json
"CMAKE_RC_COMPILER": "D:/Software/Qt/Tools/llvm-mingw1706_64/bin/llvm-windres.exe"
```

错误的 RC 路径会固化在 `build_*/CMakeFiles/rules.ninja` 里，**只重新 build 不重新 configure 无效**——删 `CMakeCache.txt` 或整个构建目录后重新 configure。

`CMakeLists.txt` 里加了防呆：C++ 是 Clang 但 RC 不是 `llvm-windres`/`llvm-rc` 时直接 `message(WARNING ...)`。

## 打包后双击弹 UAC、之后无法拖放文件

**根因**：Enigma Virtual Box 等打包器生成的单文件加载器被 Windows installer detection 判为安装程序，进程被拉到高完整性级别；资源管理器是普通完整性级别，UIPI 禁止低→高拖放，事件在 OLE 层被拒，应用收不到任何通知。

**修复**：

1. 原始 exe 已通过 `assets/DataInspector.rc` 嵌入 `asInvoker` 清单。
2. 打包后必须确认最终单文件仍带 `asInvoker`——部分打包器会生成自己的加载器外壳而不继承原清单。
3. 必要时手动补：`mt.exe -manifest DataInspector.manifest -outputresource:Packed.exe;#1`。
4. 输出文件名不要含 `setup`/`install`/`update`/`patch` 等词。
5. 应用在提权运行时会在状态栏提示"以管理员身份运行，无法从资源管理器拖放文件"——这是兜底提示，提权状态下的拖放是 Windows 设计，应用无法绕过。

## 部署后工具栏图标空白

`deploy_qt6.ps1` 必须拷贝 `plugins/imageformats/qsvg.dll` 和 `plugins/iconengines/qsvgicon.dll`。缺了它们 Qt 找不到 SVG 图像格式插件，QML 里的 `Image { source: "qrc:/icons/*.svg" }` 全部空白。

## 换构建目录后 QML 编辑器不识别 C++ 类型

首次克隆必须先 configure + build 一次 `DataInspector_qmllint`（或构建整个目标），让 CMake 生成 `.qmltypes` 和本地 `qml/.qmllint.ini`、`.qmlls.ini`。这些机器相关文件已被 gitignore，不要手改。

详见 [QML 工具与静态检查](qml-tooling.md)。
