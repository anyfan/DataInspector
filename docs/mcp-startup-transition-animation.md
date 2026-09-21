# 启动过渡动画与启动性能分析

## 性能结论

完整 QML Profiler 结果保存在工程根目录的 `1.txt`。共 1545 条记录，自身耗时合计约 1175 ms：

| 类型 | 自身耗时 | 占比 |
| --- | ---: | ---: |
| 创建 | 915.0 ms | 77.9% |
| 编译 | 199.4 ms | 17.0% |
| 绑定 | 33.8 ms | 2.9% |
| JavaScript | 26.6 ms | 2.3% |

最显眼的两项不是普通的重复控件成本，而是一次性初始化成本：

- `ToolButton.qml:24` 的 `IconLabel` 36 次调用自身 512 ms，但其中一次占 506 ms，其余 35 次合计约 6 ms，中间值只有 26.8 µs。
- `ApplicationWindow.qml:10` 两次调用自身 291 ms，其中一次占 290 ms，另一次只有 558 µs。

这说明首次触碰 Qt Quick Controls、窗口、字体/图标与渲染相关设施时，会把冷启动成本记在第一个控件上；不能通过简单删掉 ToolButton、MenuItem 或 Dialog 消除。`Main.qml` 编译自身 118 ms，另有 `SpinBox.qml` 25.3 ms、`ApplicationWindow.qml` 20.5 ms、`Calendar.qml` 13.1 ms 等编译成本。该报告来自 Debug 构建，Release 的绝对值不应直接等同，但类别和一次性冷启动特征仍有参考价值。

## 方案选择

Release 环境无法正常创建 QML Profiler 记录，因此本次优先优化**启动感知速度**而不是冒险重构 QML 控件树：

1. 先创建一个只依赖 `QtQuick` / `QtQuick.Window` 的轻量启动窗口。
2. 启动画面先渲染至少 120 ms，让用户立刻看到应用身份、图标和活动进度条。
3. 再加载较重的 `Main.qml`、Controls、Dialogs、QuickPlot 和渲染器。
4. 主界面成功创建后，启动画面执行 180 ms 淡出并销毁。

这个方案不声称减少所有初始化的实际 CPU 时间；它把不可避免的冷启动成本从白屏期间移到有反馈的过渡画面期间。主界面完成后仍会复用原有的启动参数导入逻辑。

## 实现

### `qml/Splash.qml`

新增 460×286 的无边框 `Window`：

- 只导入 `QtQuick` 与 `QtQuick.Window`，不提前触碰 Qt Quick Controls。
- 使用新版 `DataInspector.svg`，标题为 `DataInspector`，副标题为“时间序列数据查看器”。
- 四个蓝色圆点循环呼吸，底部黄色进度条往返移动，明确表示程序仍在工作。
- 暴露 `dismiss()` 函数，执行 180 ms `OutCubic` 淡出后隐藏并发出 `dismissed()`。
- 已处理 qmllint 的动画重复绑定和 delegate 非限定属性问题；圆点采用相同节奏，避免为动画引入额外绑定。

### `src/quick/main.cpp`

启动顺序从原来的“直接加载 Main”变为：

```text
QGuiApplication / AppController
        ↓
加载 Splash.qml
        ↓ 120 ms
加载 Main.qml，注入 appController
        ↓ 主窗口创建成功
启动命令行文件导入
        ↓
Splash 淡出并销毁
```

两处加载失败都写入原有的 `startup-error.log`，错误处理没有被启动动画吞掉。命令行数据文件仍在主窗口和子图创建完成后调用 `openStartupFiles()`，因此拖拽到 exe 启动加载的行为保持不变。

### `CMakeLists.txt`

- 把 `qml/Splash.qml` 加入 `DI_QML_FILES`，让 Qt QML cache 在正式构建时预编译它。
- 把 `assets/icon/DataInspector.svg` 加入图标资源列表，启动画面使用与 exe 相同的图标。

## 验证

- Debug 增量构建成功，包含 `Splash_qml.cpp` 的 QML cache 编译和最终链接。
- Release `DataInspector` 目标成功构建，无错误。
- Debug CTest **10/10 通过**，包括原有加载/会话/渲染测试和启动拖放测试。
- `QT_QPA_PLATFORM=offscreen` 启动 Release exe 8 秒烟测正常运行，未生成 `startup-error.log`。
- QML Profiler 产生的原始完整结果保留为工程根目录 `1.txt`，本文件只记录结论和实现，不复制原始大表。

## 后续可选优化

- 若以后能对 Release 创建有效 QML Profiler，可再确认 qmlcache 是否消除了约 199 ms 的 Debug 编译段。
- 可以把 120 ms 作为配置常量，根据不同机器的首帧耗时进一步调整；不建议继续增加固定延迟。
- 若要降低真实 CPU 时间，需要针对 Qt Quick 平台窗口/RHI 首次初始化做平台级预热或更深的渲染架构调整，单纯延迟 Dialog/Menu 不会解决主要成本。
