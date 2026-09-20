# QML 静态分析警告修复

## 前置提交与范围

按要求先提交了上一轮已验证的会话功能：

- `88e72d5` — `feat: 新增会话保存与恢复`
- 提交后工作区干净；随后才开始本轮 QML 修复。
- 本轮修复未另行提交、未推送，不混入会话功能提交。

目标是修复类型、作用域和导入配置，而不是禁用、降级或屏蔽 warning。

## 根因与改动

### 1. 隐式上下文变量与跨组件作用域

- `Main.qml` 使用 `required property AppController appController`，所有引用明确为 `window.appController`。
- 主程序用 `QQmlApplicationEngine::setInitialProperties` 注入实例，测试用 `createWithInitialProperties`，不再依赖主窗口的上下文全局变量。
- `QuickPlot.controller` 也改为 `AppController` 类型。
- 四个 QML 文件显式声明 `pragma ComponentBehavior: Bound`，约束委托/内联组件使用定义处的上下文；委托模型数据仍由 required 属性明确接收。
- 补全父级颜色属性、坐标轴刻度及游标标签背景的 ID 限定，避免隐式跨层级查找。

### 2. C++ 类型元数据

- 新增 `src/quick/qmltypes.h`，用 `QML_FOREIGN` 声明 `PlotItem`、`AppController` 和 `SignalModel`，由 CMake 自动生成注册代码与 `.qmltypes`。
- 控制器和模型在 QML 中不可创建，生命周期仍由 C++ 管理；主程序不再重复手工注册 `PlotItem`。
- 显式声明 `QtQuick`、`QtQml.Models` 依赖，供工具解析 `QQuickItem` 等基类。
- Qt 6.8 注册模板即使对不可创建类型也会实例化 `QQmlElement<T>` 子类，因此移除 `AppController`、`SignalModel` 的 C++ `final`，并在头文件解释原因；这不改变 QML 中不可创建的约束。

### 3. 静态类型与名称冲突

- `Repeater.itemAt()` 的返回值先转换为 `QuickPlot`，再访问 `renderer` 和 `measuredAxisLeft`，并保留空值检查。
- 文本输入焦点保护使用 `TextInput` / `TextEdit` 类型判断，不再读取通用 `QQuickItem` 上不存在的 `selectedText` 属性。
- 拖拽预览 ID 改为 `legendDragPreview`，避免与 `PlotLegend.dragPreview` 属性同名；较新的编辑器语言服务也能正确解析。
- 移除 `QuickPlot.qml` 未使用的 `QtQuick.Layouts` 导入。

### 4. 工具与编辑器导入路径

- 模块统一生成到 `<构建目录>/qml/DataInspector`，并通过模块 `IMPORT_PATH` 提供给工具。
- CMake 从 `qml/qmllint.ini.in` 生成本地 `qml/.qmllint.ini`；设置当前构建的单一导入根和 `MaxWarnings=0`。Qt 6.8 的 tooling settings 会把 INI 列表值转成字符串，因此不使用多个导入路径的 INI 列表。
- 启用 Qt 的 `.qmlls.ini` 生成；上述机器相关配置均被 Git 忽略。
- 当前 ShunCode Qt QML 扩展使用单独下载的较新版语言服务器，并带有自己的启动参数。已在本地 `.vscode/settings.json` 的 `qt-qml.qmlls.additionalImportPaths` 中补充构建目录；设置生效后重新请求了四个文件的语言服务符号与诊断。
- `.vscode/settings.json` 原本就被忽略，本次只追加导入路径，未覆盖已有 CMake 配置；该本地设置不包含在 Git diff 中。README 给出了其他机器的配置方法。

## 检查方式

完成 CMake 配置后：

```sh
cmake --build build_qt6-release --target DataInspector_qmllint
cmake --build build_qt6-release --parallel 3
ctest --test-dir build_qt6-release --output-on-failure
```

Debug 使用相同命令，将目录替换为 `build_qt6-debug`。检查目标覆盖全部四个 QML 文件，任何 warning 都会导致失败。

也验证直接调用 Qt 6.8.3 `qmllint -W 0` 检查四个源码文件，不依赖调用者额外传 `-I`；导入路径来自生成的 INI。

## 回归覆盖

- 现有八套 CTest 全量回归，包含真实 `Main.qml` 与 `QuickPlot.qml` 对象测试。
- 更新主窗口测试的显式控制器注入，并断言属性指向同一个 C++ 实例。
- 增加 `TextInput`、`TextEdit` 获得焦点后调用游标焦点恢复函数仍不被抢焦点的断言。
- 工具栏模式与会话恢复集成测试额外收集 `QQmlEngine::warnings`，断言 QML 创建/交互过程中没有运行时警告。
- 原有图例拖拽、游标、坐标缩放、信号树和会话恢复行为继续受现有测试覆盖。

## 最终结果

验证环境：Windows、Qt 6.8.3 / LLVM-MinGW 17，MAT 开启，GPU 回归开关关闭。

| 项目 | 最终结果 |
| --- | --- |
| 编辑器 error / warning | **0 / 0**，最初返回 163 个 warning；重新请求四个 QML 文件的语言服务后再次确认 |
| Release `DataInspector_qmllint` | **通过，零 warning** |
| Debug `DataInspector_qmllint` | **通过，零 warning** |
| 直接 `qmllint -W 0` 检查四个源码文件 | **通过，exit 0** |
| Release 构建 + CTest | **通过，8/8，46.98 秒** |
| Debug 构建 + CTest | **通过，8/8，48.27 秒** |
| 生产可执行文件启动冒烟 | **通过**，Release 程序在 offscreen / software 后端存活 5 秒，无 QML 启动/绑定错误；检查后仅终止本次启动的进程 |
| `git diff --check` | **通过**；Git 的 LF → CRLF 提示不属于空白检查失败 |

最终构建覆盖最后的预览 ID 重命名、编辑器导入路径及新增测试断言。前置提交保持为 `88e72d5`。

## 验证边界

- QML 静态分析零 warning 不等同于所有原生 C++ 编译器输出零 warning；Qt 自身生成的 C++ 代码仍可能出现 `Q_GLOBAL_STATIC` 可变参数宏提示。
- 离屏测试环境可能产生字体路径/Windows 主题等 Qt 平台日志；这些与 `QQmlEngine::warnings` 的脚本/绑定警告不同。
- 本轮不替代真实显卡、鼠标操作和部署目标上的人工 GUI 验收。
