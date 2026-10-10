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

`objectdata_test` 验证拆位/符号位段、表达式与时间基、对象归属和会话校验、来源重新绑定/移除、后台结果、保存恢复、阶梯短脉冲和对象管理 QML（选中名称可见、信号树绑定、自动命名、真实鼠标双击重命名、固定飞机参数保护与坐标/姿态配置）。设置 DI_OBJECT_UI_CAPTURE_DIR 可输出浅色、深色、飞机页面的离屏截图；截图需与所用 Scene Graph 后端一同记录。

| 套件 | 覆盖范围 |
| --- | --- |
| `trajectory_test` | 时间基、缺失断线、空间简化误差/尖峰、百万点旋转成本、连续请求帧更新、投影比例/精度、经纬度投影及零定位只排除自适应、双参数航迹、屏幕方向旋转与画面中心、指针缩放（含连续放大至上限、超过四视口偏移和立即拖动）、四向平移、窗口事件路由、显示帧快照、控制器来源隔离及飞机对象绑定联动、小坐标轴左键旋转、原始读数、小子图布局、v1–v5 会话（含大平移往返）及源码/编译 QML 选择飞机对象、配置折叠、对象名称及来源刷新 |
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
| `startupdrop_test` | 启动后窗口拖放事件投递 |

`appcontroller_test::signalTreeDragAddsToTargetPlot` 使用实际窗口鼠标按下、移动和松开验证信号树拖拽：搜索后的源行映射、目标边框高亮、跨图添加保留原图绑定、重复添加、图外取消及无效索引。信号名称需要拖动超过 8 个逻辑像素；测试从名称区域开始，不触发复选框或线型编辑。

## GPU 测试

航迹旋转交互回归额外覆盖：四个预设视角下直接抓取 X/Y/Z 环（含侧视退化投影），固定轴方向不变，平移/缩放/深度偏移后投影支点保持子图中心，拖回起点恢复方向与平移；环的命中容差、固定屏幕尺寸、小视口裁剪及连续两圈角度；源码及编译 QML 通过窗口事件验证直接拖环、环内自由旋转、环外平移、Shift 平移、Alt 自由旋转和中心参考显隐。二维无操纵器且中键不改变相机。`trajectory_raster_test` 另验证硬件绘制的环高亮增粗和实际拖环后的视角变化。

角落合并控件额外验证：默认收起、靠近展开、拖动离开仍展开、松开后自动收起、resize 后清除旧悬停状态、普通画布中央左键平移。GPU 检查环收起后红色覆盖明显减少，且画面中央区域像素保持完全相同，证明控件不覆盖中央航迹。

旋转方向/读数回归：鼠标进入或稀疏事件跨过环中心不引入半圈；新旋转手势拒绝上一轮待显示帧；绝对 Rz·Ry·Rx 欧拉角在普通/±90° Y 姿态可重建相机方向，连续两次抓取、松开、适应视图不重置，正视 YZ 回到 NED 零姿态（北内/东右/地下），俯视为 Y=90°、侧视为 Z=90°，三轴单独正转读数为正；源码/编译 QML 读数跟随实际已显示帧。

密集测试可追加 `--compressed`，将 Y 轴设为约 -8000–198000，使 GPS 毫秒曲线只有约 4 像素高；仍检查两种宽度、五档亚像素偏移下的上下沿一致性。

`plotitem_raster_test --dense` 检查 247511 点合成锯齿波；`--dense-csv path` 读取带表头的 time,value 两列 CSV，检查 3300–3900 秒恒定极值区间，适用于 `11040237.DAT.mat` 的 GPS 毫秒信号。CPU `denseEnvelopeOnlyAddsCoverage` 对振荡、阶跃、单调和孤立尖峰逐顶点验证原几何保留，同时检查 NaN 分段、虚线与放大禁用条件。

`plotitem_raster_test --step-csv path` 可读取带表头的 time,signal1,signal2 三列 CSV，在 290–1010 秒视窗内验证两条信号最大跳变的连线覆盖；用于发动机负载测试的航向/俯仰回归。默认不带参数仍运行原有光栅覆盖测试。CPU 套件的 `lodGeometryPreservesStepConnections` 检查上升/下降阶跃在 LOD 桶边界附近的几何连续性。

阶跃 GPU 检查同时按像素灰度积分测量竖线宽度：2 逻辑像素画笔允许不超过 `2×DPR+0.75` 物理像素的覆盖宽度，防止“连线还在但已加粗”的回归。`denseEnvelopeOnlyAddsCoverage` 还覆盖带微小交替抖动的阶跃，要求跨越阶跃的桶不生成包络，最终几何与普通折线路径一致。

默认 offscreen 平台运行，不等同人工 GUI 验收。需要 GPU 回归时：

```powershell
cmake -S . -B build_gpu -DENABLE_GPU_TESTS=ON
```

GPU 测试需要真实 Scene Graph 后端。

## 验证边界

`appcontroller_test::aboutPageShowsBuildInfoAndReleaseNotes` 覆盖关于按钮打开/关闭、构建信息与离线更新记录显示、构建信息复制。

受限环境若默认临时目录无法写入，可将 `TEMP`/`TMP` 指向构建目录下自行创建的 `test-temp` 后运行测试。Windows 上需要捕获 Qt 测试输出时可设置 `QT_FORCE_STDERR_LOGGING=1`。

- 离屏 QML 加载测试能捕获绑定错误和类型错误，但不能验证帧率、字体渲染、HiDPI 观感。
- 百万/千万点帧率基准需要专门测试数据，默认回归不跑。
- 部署包需在干净 Windows 环境上人工启动确认无缺失 DLL。

航迹默认绑定回归：新视图经纬度默认、按勾选顺序填空轴、三轴满后不覆盖、取消/再勾选补空轴、手动留空与模式往返保持、已有时间图来源自动填轴、子图隔离；源码/编译 QML 验证各角度数值始终使用对应轴色。

## 多航迹与测量姿态回归

trajectory_test 增加公共原点和范围、不同起点与采样率、错误/隐藏隔离、缓存复用和删除原点重建、活动列表管理、文件来源重映射、v1–v5 单航迹迁移、v6 列表/样式/姿态往返和严格拒绝、导出源列去重及失败恢复保留。姿态覆盖 NED/FRD 零位、三轴 ±90°、复合 Rz·Ry·Rx/Rx·Ry·Rz、度/弧度、wxyz/xyzw、正反变换、归一化四元数、缺口与独立采样时间差、相机不替代测量、固定像素大小。源码与编译 QML 通过管理控件添加、配置姿态、选择、隐藏和删除航迹。

trajectory_raster_test 在原 CAD 环光栅用例后，验证两条不同起点路径的颜色覆盖、姿态开关的像素差异、实际航向变化和缺失姿态消除模型；Windows 使用 QSG_RHI_BACKEND=d3d11。独立窗口测试不等同人工全功能验收。

multiTrajectoryCost 使用 8 条各 100000 点合成地理路径，记录准备和 20 次交互预览耗时，无固定帧率断言；不能沿用百万点单航迹结果承诺多航迹性能。原始 Store、路径缓存、级别索引、后台任务及 SG 上传都计入实际使用成本，仍需真实数据/硬件专项测试。

姿态显示修复回归：源码/编译 QML 检查旋转顺序下拉两个可见选项均有文字、表单宽度不溢出、底部方向设置可滚动到达、重开恢复顶部；D3D11 对飞机实际像素差异测量逻辑尺寸不超过 42 像素并验证深色描边覆盖，继续检查测量航向变化和缺失隐藏。

中间缩放回归：`plotitem_raster_test --dense --zoomed` 使用 1250–2700 秒视窗，追加 `--compressed` 检查共享 Y 轴。`--dense-csv path --zoomed` 用同样 1450 秒跨度的 3250–4700 秒视窗，在已知恒定极值的 3300–3900 秒区间检查；原始 GPS 数据约 2152.14 秒的真实高样本不作为幽灵凸点删除。CPU `denseOscillationAcrossBucketBoundaries` 检查单桶不足一周期时仍识别振荡，现有阶跃/抖动/NaN/虚线/放大回归继续保留。

选中加粗回归：`plotitem_selected_dense_raster_test` 使用 `--dense --narrow --selected --stepped-millis`，模拟 40 ms 采样、100 ms 平台保持的毫秒锯齿波，在 2600–3100 秒范围检查选中后的四像素线宽。`--dense-csv hmillisec.csv --narrow --selected` 对用户 p3 H_milliSec 原始列运行同一检查；`--compressed` 可同时检查共享大范围 Y 轴。CPU `denseEnvelopeUsesEffectiveStrokeWidth` 覆盖多尺度候选及两档实际线宽，要求原折线顶点完整保留。

## UI 配置复用回归

飞机字段生命周期回归检查默认仅经纬高、按启用配置创建 XYZ/姿态字段、飞机→常规→飞机不增字段且名称/身份保留、常规副本恢复飞机、预置参数置顶及删除保护、关闭/重启配置复用已有字段、v10 常规预置角色会话往返。规则窗口检查 footer 只有两个按钮，避免标准按钮和自定义按钮同时出现。

对象与航迹来源交互：`objectdata_test::objectManagerQml` 检查对象自动命名、真实双击重命名、切换对象后的信号树勾选隔离、真实鼠标拖到字段绑定（无需预选字段），以及展开的下拉选项在浅色/深色模式下可见。设置 `DI_OBJECT_UI_CAPTURE_DIR` 可保存离屏 software 截图。`trajectory_test::qmlModeSwitchAndSignalDialog` 在源码和编译 QML 下检查飞机对象选择、对象来源与名称更新、添加多对象航迹和样式配置。旧航迹直接来源 API 仍有控制器和内核覆盖，当前 UI 的位置及姿态来源统一通过对象编辑。

`appcontroller_test::preciseCursorInputSnapsSynchronizesAndCancels` 检查原始样本吸附、ΔT、无效输入、真实双击示数/Enter/Esc 与视窗外定位。`session_test::viewTemplatesMatchRemapAndUndo` 检查换文件/换列序、仅匹配引用信号、重复/非整数映射拒绝、航迹映射、固定/自适应范围及一次撤销；现有 Main QML 用例检查匹配对话框的完成状态与应用。`trajectory_test::qmlModeSwitchAndSignalDialog` 在源码和 qmlcachegen 两种模式下检查配置折叠、画布尺寸、子图偏好隔离及游标输入。
