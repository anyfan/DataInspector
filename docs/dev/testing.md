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
| `plotitem_blend_test` | 曲线绘制层级（AmplitudeLayers 排序置顶） |
| `plotitem_raster_test` | GPU 光栅路径（需 `-DENABLE_GPU_TESTS=ON`） |
| `appcontroller_test` | 控制器集成：布局、自适应、游标、回退、会话入口、QML 加载 |
| `session_test` | 会话往返、结构校验、失败保留、相对路径、子图生命周期 |
| `signalselection_test` | 信号树模型、祖先路径、选择/拖拽 |
| `dataloadworker_test` | CSV 引号/数值解析、MAT 复数/稀疏/空矩阵防护 |
| `xlsxwriter_test` | XLSX 写入、工作表拆分 |
| `matwriter_test` | MAT5 流式写入、pN/pN_title、取消保留 |
| `startupfiles_test` | 命令行参数解析、开关跳过、会话优先 |
| `startupdrop_test` | 启动后窗口拖放事件投递 |

## GPU 测试

默认 offscreen 平台运行，不等同人工 GUI 验收。需要 GPU 回归时：

```powershell
cmake -S . -B build_gpu -DENABLE_GPU_TESTS=ON
```

GPU 测试需要真实 Scene Graph 后端。

## 验证边界

- 离屏 QML 加载测试能捕获绑定错误和类型错误，但不能验证帧率、字体渲染、HiDPI 观感。
- 百万/千万点帧率基准需要专门测试数据，默认回归不跑。
- 部署包需在干净 Windows 环境上人工启动确认无缺失 DLL。
