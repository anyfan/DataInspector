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
