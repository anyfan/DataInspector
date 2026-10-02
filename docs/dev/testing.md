# 测试

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联目录**：`tests/`、`CMakeLists.txt` · **新增功能后必须跑对应套件**

## 运行

```powershell
cmake --build build_qt6-debug --target DataInspector appcontroller_test session_test --parallel 4
$env:QT_QUICK_CONTROLS_STYLE = 'Fusion'
ctest --test-dir build_qt6-debug --output-on-failure
```

Release 把目录换成 `build_qt6-release`。

## CTest 套件

| 套件 | 覆盖范围 |
| --- | --- |
| `trajectory_test` | 时间基、缺失断线、空间简化误差/尖峰、百万点旋转成本、连续请求帧更新、投影比例/精度、经纬度投影及零定位只排除自适应、双参数航迹、屏幕方向旋转与画面中心、指针缩放（含连续放大至上限、超过四视口偏移和立即拖动）、四向平移、窗口事件路由、显示帧快照、子图可用信号隔离与局部下拉绑定/交换/解绑、小坐标轴左键旋转、原始读数、小子图布局、v1–v5 会话（含大平移往返）及源码/编译 QML 直接进入、坐标配置、真实下拉点击及绑定名称刷新 |
| `trajectory_raster_test` | D3D11 等轴测/俯视/自由旋转轨迹像素与视角变化（GPU） |
| `rendercore_test` | LOD 构建、极值索引、范围查询、几何生成、浮点桶边界 |
| `plotitem_lod_test` | PlotItem LOD 调度、视窗投影 |
| `plotitem_blend_test` | 曲线绘制层级（AmplitudeLayers 排序、选中置顶、视窗变化、透明颜色） |
| `plotitem_raster_test` | GPU 光栅路径（需 `-DENABLE_GPU_TESTS=ON`） |
| `plotitem_dense_raster_test` | 密集锯齿波两种预览宽度、五档亚像素偏移的边缘一致性（GPU） |
| `plotitem_compressed_raster_test` | 大范围共享 Y 轴下压缩密集波形的边缘一致性（GPU） |
| `appcontroller_test` | 控制器集成：布局、自适应、游标、回退、会话入口、QML 加载 |
| `session_test` | 会话往返、结构校验、失败保留、相对路径、子图生命周期 |
| `signalselection_test` | 信号树模型、祖先路径、选择/拖拽 |
| `dataloadworker_test` | CSV 引号/数值解析、MAT 复数/稀疏/空矩阵防护 |
| `xlsxwriter_test` | XLSX 写入、工作表拆分 |
| `matwriter_test` | MAT5 流式写入、pN/pN_title、取消保留 |
| `startupfiles_test` | 命令行参数解析、开关跳过、会话优先 |
| `startupdrop_test` | 启动后窗口拖放事件投递 |

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

- 离屏 QML 加载测试能捕获绑定错误和类型错误，但不能验证帧率、字体渲染、HiDPI 观感。
- 百万/千万点帧率基准需要专门测试数据，默认回归不跑。
- 部署包需在干净 Windows 环境上人工启动确认无缺失 DLL。

航迹默认绑定回归：新视图经纬度默认、按勾选顺序填空轴、三轴满后不覆盖、取消/再勾选补空轴、手动留空与模式往返保持、已有时间图来源自动填轴、子图隔离；源码/编译 QML 验证各角度数值始终使用对应轴色。
