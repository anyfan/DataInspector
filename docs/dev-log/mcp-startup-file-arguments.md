# 拖拽到可执行文件启动加载

## 需求

把数据文件拖到 `DataInspector.exe` 上（或用"打开方式"选择本程序、双击已关联的数据文件）时，程序启动后自动导入这些文件，不需要再手动打开文件选择器。

## 实现方式

Windows 资源管理器把拖到可执行文件上的路径作为命令行参数传给进程，因此这里不需要新的拖放协议，只需解析 `argv` 并在事件循环启动后触发既有导入链路。

### 新增 `src/quick/startupfiles.h` / `src/quick/startupfiles.cpp`

纯解析单元，不依赖 GUI，便于单测覆盖：

- `parseStartupFiles(arguments)` 从 `argv[1..]` 提取文件参数，返回 `StartupFiles{dataFiles, sessionFile, rejected}`。
- 以 `-` 开头的参数按开关处理；`-platform`、`-style`、`-qmljsdebugger` 等会吃掉下一个参数的开关列在 `optionTakesValue()` 中，避免把 `offscreen` 之类的值误判成文件；写成 `-opt=value` 形式时不跳过下一个参数。
- 相对路径按进程工作目录解析（资源管理器传绝对路径，命令行可能传相对路径）。
- 只保留真实存在且可读的文件，按 `canonicalFilePath()` 去重，保持命令行顺序。
- 后缀不区分大小写：数据文件为 `csv` / `txt` / `xlsx` / `mat`，会话文件为 `disession` / `json`，与文件选择器和会话恢复使用的集合一致。
- 其余已存在但后缀不支持的文件记入 `rejected`，仅用于状态栏提示，不阻断启动。

### `AppController::openStartupFiles(const QStringList &arguments)`

位于 `src/quick/appcontroller_loading.cpp`，是启动参数与既有导入链路之间唯一的接合点：

- 参数中含会话文件时调用 `restoreSession()`，并忽略同时传入的散装数据文件 —— 会话会重建整个工作区，混入额外文件会让恢复结果不可预期。
- 否则把数据文件交给 `loadFiles()`，复用同一套后台加载队列、批量进度、去重、状态栏汇总和首个文件的共享 X 轴初始化逻辑。
- 没有可用文件时返回 0，仅在存在被忽略文件时更新状态栏。
- 返回值为接受的文件数（会话成功恢复记为 1），便于测试和后续调用方判断。

因为走的是 `loadFiles()`，QML 拖放、文件选择器和启动参数三条入口共用同一实现，行为天然一致。

### `src/quick/main.cpp`

QML 根对象创建成功后，用 `QTimer::singleShot(0, ...)` 把 `openStartupFiles(app.arguments())` 投递到事件循环：

- 窗口与子图已经存在，加载完成后的 `refreshPlot()` 能正常绑定曲线。
- 状态栏与进度条从一开始就能显示导入进度。
- 根对象创建失败时提前 `return 1`，不会触发导入。

## 行为说明

- 多选文件一次拖到 exe 上：资源管理器一次传入全部路径，按批量加载排队导入，进度条显示整批总进度。
- 数据文件与会话文件混合传入：只恢复会话。
- 参数中含重复路径或同一文件的不同写法：按规范化路径去重，只导入一次。
- 路径不存在、不可读或后缀不支持：跳过，不影响其余文件，也不阻止程序启动。
- 程序已在运行时再次拖文件到 exe：Windows 会启动新实例，本次未实现单实例转发，与改动前一致。

## 测试

新增 `tests/startupfiles_test.cpp`，注册为 CTest `startupfiles_test`（`-platform offscreen`）：

- `parsingSkipsSwitchesAndKeepsExistingDataFiles` —— 跳过开关及其取值、相对路径解析、重复路径去重、后缀大小写不敏感、不存在的文件与不支持的后缀分流。
- `droppedArgumentsLoadOnStartup` —— 通过启动参数导入两个文件后 `loadedFileCount()` / `signalCount()` 正确，无参数时不进入加载状态。
- `sessionArgumentTakesPrecedenceOverDataFiles` —— 会话参数优先，散装数据文件被忽略，重命名等会话内容正确恢复。

`cmake --build --preset qt6-clang-debug` 无警告通过（`-Wall -Wextra -Wpedantic`）；`ctest` 9/9 全部通过；`get_diagnostics` 无错误告警。另以 `QT_QPA_PLATFORM=offscreen` 带两个数据文件参数实际启动 `build_qt6-debug/bin/DataInspector.exe`，进程正常运行且未生成 `startup-error.log`。

## 改动文件

- `src/quick/startupfiles.h`、`src/quick/startupfiles.cpp`（新增）
- `src/quick/appcontroller.h`、`src/quick/appcontroller_loading.cpp`
- `src/quick/main.cpp`
- `tests/startupfiles_test.cpp`（新增）
- `CMakeLists.txt`、`readme.md`

## 未做的部分

- 未注册文件关联：`.csv` / `.txt` / `.xlsx` / `.mat` 与 `DataInspector.exe` 的关联需要安装程序或注册表写入，属于打包环节；关联建立后双击文件即可复用本次的参数解析路径。
- 未实现单实例与已有窗口转发，拖到 exe 会新开进程。
- 未处理超长命令行导致资源管理器截断参数的极端情况（Windows 命令行长度上限约 32767 字符）。
