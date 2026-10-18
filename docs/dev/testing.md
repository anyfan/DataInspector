# 测试

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联目录**：`tests/`、`CMakeLists.txt` · **新增功能后必须跑对应套件**

## CPU 性能基准

`performance_benchmark` 随 `BUILD_TESTING=ON` 构建，默认不加入 CTest，避免日常回归反复运行大数据基准。以下示例在 PowerShell 中运行，需让 Qt DLL 目录位于 PATH：

```powershell
$env:PATH = 'D:/Software/Qt/6.8.3/llvm-mingw_64/bin;' + $env:PATH
cmake --build --preset qt6-clang-release --target performance_benchmark
./build_qt6-release/performance_benchmark.exe --samples 1000000 --signals 4 --output build_qt6-release/perf-million.json
./build_qt6-release/performance_benchmark.exe --samples 10000000 --signals 2 --output build_qt6-release/perf-ten-million.json
./build_qt6-release/performance_benchmark.exe --input path/to/flight.mat --output build_qt6-release/perf-real.json
```

记录原始数据生成或真实加载、Store/索引准备、20 次视窗变化的冷 LOD、缓存查找及几何构建中位数/P95、缓存命中数/字节数、10 万信号树构建/搜索，以及 Windows 工作集/峰值工作集。用 `--iterations`、`--tree-signals` 调整规模。窗口宽度固定 1200、每次视窗覆盖一半时间范围；真实数据需有非零时间跨度。合成规模最多每序列 1000 万点、总共 8000 万点。报告标记 Debug/Release，不设置机器相关耗时门槛。

基准直接调用 CPU 内核，不包含事件输入延迟、SG 上传或 GPU 帧率，也不代表当前窗口实际显示效果；硬件视觉与帧率应独立验收。

## 运行

```powershell
cmake --build --preset qt6-clang-debug --target DataInspector appcontroller_test session_test
$env:QT_QUICK_CONTROLS_STYLE = 'Fusion'
ctest --test-dir build_qt6-debug --output-on-failure
```

构建预设自动按逻辑 CPU 核数并行。Release 构建使用 `qt6-clang-release` 预设，测试目录换成 `build_qt6-release`。

默认测试数据在测试内部生成，不依赖本机未提交的 `test_file/`。多工作表导入回归生成三个不同时间基的工作表（1、2、17），共 80 个信号和 12 行数据，在干净 checkout 和 GitHub Actions 上同样运行。真实大文件性能测试仍通过显式环境变量启用。

## CTest 套件

| 套件 | 覆盖范围 |
| --- | --- |
| `trajectory_test` | 时间基、缺失断线、空间简化误差/尖峰、百万点旋转成本、连续请求帧更新、投影比例/精度、经纬度投影及零定位只排除自适应、双参数航迹、屏幕方向旋转与画面中心、指针缩放（含连续放大至上限、超过四视口偏移和立即拖动）、四向平移、窗口事件路由、显示帧快照、控制器来源隔离及飞机对象绑定联动、小坐标轴左键旋转、原始读数、小子图布局、当前会话（含大平移往返）及源码/编译 QML 选择飞机对象、配置折叠、对象名称及来源刷新 |
| `trajectory_raster_test` | D3D11 等轴测/俯视/自由旋转轨迹像素与视角变化（GPU） |
| `rendercore_test` | LOD 构建、极值索引、范围查询、几何生成、浮点桶边界 |
| `plotitem_lod_test` | PlotItem LOD 调度、视窗投影 |
| `plotitem_blend_test` | 曲线绘制层级（AmplitudeLayers 排序、选中置顶、视窗变化、透明颜色） |
| `plotitem_raster_test` | GPU 光栅路径（需 `-DENABLE_GPU_TESTS=ON`） |
| `plotitem_dense_raster_test` | 密集锯齿波两种预览宽度、五档亚像素偏移的边缘一致性（GPU） |
| `plotitem_zoomed_raster_test` / `plotitem_zoomed_compressed_raster_test` | 中间缩放的跨桶振荡，正常/压缩高度，两种宽度与五档亚像素偏移（GPU） |
| `plotitem_compressed_raster_test` | 大范围共享 Y 轴下压缩密集波形的边缘一致性（GPU） |
| `appcontroller_test` | 控制器集成：布局、自适应、游标、回退、会话入口、QML 加载 |
| `session_test` | 会话往返、结构校验、失败保留、相对路径、子图生命周期 |
| `signalselection_test` | 信号树模型、祖先路径、选择/拖拽 |
| `dataloadworker_test` | CSV 引号/数值解析、MAT 复数/稀疏/空矩阵防护 |
| `xlsxwriter_test` | XLSX 写入、工作表拆分 |
| `matwriter_test` | MAT5 流式写入、pN/pN_title、取消保留 |
| `startupfiles_test` | 命令行参数解析、开关跳过、会话优先 |
| `automation_test` | 本机协议、认证/帧限制、原始统计与分页、设置/恢复和旧版本拒绝 |
| `automation_cli_test` | 可选 Python3 3.9+；实际程序 + CLI，中文路径/参数文件、分析、飞机对象/经纬高绑定、航迹准备与视角、PNG 捕获、稳定字段会话恢复和错误退出码（offscreen/software） |
| `startupdrop_test` | 启动后窗口拖放事件投递 |

automation_cli_test 另验证 clear 删除全部对象/数据；merge_signals 的整批拒绝无部分提交、交错采样正确合并、同时间不同值报错及保存恢复后错误仍可追溯。

`appcontroller_test::signalTreeDragAddsToTargetPlot` 使用实际窗口鼠标按下、移动和松开验证信号树拖拽：搜索后的源行映射、目标边框高亮、跨图添加保留原图绑定、重复添加、图外取消及无效索引。信号名称需要拖动超过 8 个逻辑像素；测试从名称区域开始，不触发复选框或线型编辑。

## 核心回归范围

- `session_test`：当前 v18 往返；全部旧版本/非法版本拒绝；失败保留、原子保存、重定位、修改状态、退出确认；独立 v3 模板匹配/撤销/严格引用校验，拒绝旧 v1/v2。
- `objectdata_test`：独立处理与对象生命周期隔离、增量依赖/取消/预算、来源移除闭包及图例清理；规则参数严格校验；X/Y 丢包三指标、窗口边界/回绕/歧义、单输出及双输出拒绝；规则 Pane 的真实鼠标绑定、草稿与大信号树导航。
- `trajectory_test`：多航迹、公共原点/缓存、位置/姿态时间基、相机与操纵环、来源重映射和当前格式往返；源码及编译 QML 两种加载方式。
- `automation_test` / `automation_cli_test`：协议和任务状态、幂等/取消、飞机映射、合并冲突的整批拒绝、逻辑导出、实际程序与标准库 CLI 集成。
- `appcontroller_test` / `signalselection_test`：游标原始吸附/同步、实际跨图拖拽、树展开/搜索/定位与来源隔离。

具体断言以 `tests/` 内测试函数为准。新增行为更新对应套件，不将每次开发记录追加为现行测试规范。`DI_OBJECT_UI_CAPTURE_DIR` 可输出对象页面离屏截图。

## GPU 与真实数据

GPU 光栅测试默认关闭，需要硬件 Scene Graph 后端；配置 `-DENABLE_GPU_TESTS=ON` 后构建并运行。离屏 software 测试不能替代此检查。渲染相关修改须按 `plotitem_raster_test` 参数选择阶跃、密集、压缩视窗、中间缩放或选中加粗场景；`trajectory_raster_test` 验证多航迹、姿态和旋转环像素行为。

百万/千万点、真实数据、SG 上传、GPU 帧率和 HiDPI 观感需独立验收，默认 CTest 不承诺这些结果。部署包还需干净 Windows 环境启动检查。

## 配置查询基准

`configuration_benchmark` 随 BUILD_TESTING 构建，不注册默认 CTest。合成原始信号和处理配置，在相同当前元数据序列化基础上比较全量候选展开与摘要加一个活动配置查询；先逐项核对全部配置摘要/候选一致，再交替顺序计时，预热一次，报告中位数与候选列表条目数。

```powershell
./build_qt6-debug/configuration_benchmark.exe -platform offscreen --signals 10000 --configs 16 --iterations 7 --output build_qt6-debug/configuration-query.json
```

数值只代表该合成配置查询，不包含 GPU、交互延迟或真实数据加载；候选条目数不等同进程内存。

## 合并性能基准

`merge_benchmark` 不注册进默认 CTest，显式提供数据并串行比较：

```powershell
./build_qt6-debug/merge_benchmark.exe --input D:/data/log.mat --mode legacy --output ./build_qt6-debug/merge-old.json
./build_qt6-debug/merge_benchmark.exe --input D:/data/log.mat --mode shared --output ./build_qt6-debug/merge-new.json
```

核对 rawSignals、mergeRules、outputSamples、errorCount 和 sha256，再比较 evaluationMs；加载耗时单独报告。peakWorkingSetBytes 是进程历史峰值，包含加载。不能将共享时间轴收益外推到无共享时间数组的数据。

## 受限环境

若 QTemporaryDir 能创建目录但文件 open 失败，先在构建目录创建 test-temp，将当前进程 TEMP/TMP 指向该绝对路径；不要放宽生产代码权限。QtTest 输出不可见时使用 `-o 路径,txt`，或设置 QT_FORCE_STDERR_LOGGING=1。
