# 代码分类与历史设计清理（2026-10-01）

- 将 PlotItem 按生命周期/视图、游标、输入交互、Scene Graph 渲染分为四个实现文件，复用同一类和原有锁。
- 删除已从界面停用的 Darken/Lighten 混色、自定义 PlotBlendMaterial 着色器；当前两种曲线层级策略使用 Qt 内置 QSGFlatColorMaterial，保留 AmplitudeLayers=3。
- 删除没有调用方的 cursorEnabled/cursorX 属性、对应重复状态和游标二维读数缓存；双游标位置、原始样本查询、QML 读数和会话恢复保留。
- 删除没有调用方的控制器全选入口、冗余计数转发、模型全选实现和 color 别名；保留界面使用的 checkedCount。
- 删除空 hoverMoveEvent 和对应无效的悬停事件订阅。
- GPU 回归保留原有绑定顺序、幅值排序、选中置顶与可见窗口变化检查，将历史混色检查换为透明颜色的标准 alpha 混合检查。
- 同步 CMake 共用源文件列表、项目代码地图和现行架构/构建/测试文档。会话 JSON 格式不变。

## 验证

- Qt 6.8.3 / LLVM-MinGW Debug 预设 configure 与全目标 build 通过。
- DataInspector_qmllint 通过，零警告。
- Fusion 样式下默认 CTest 10/10 通过，包括控制器离屏 QML、游标/轴交互、会话恢复、信号选择和加载/导出。
- Intel Iris Xe / D3D11 硬件后端：plotitem_blend_test 通过，透明叠加颜色为 #7f3fbf；绑定顺序、幅值排序、选中置顶及视窗变化检查通过。
- plotitem_raster_test 默认、--dense、--dense --compressed 三种硬件检查均通过，密集和压缩波形的各档偏移边缘差异为 0。
- GPU 运行报告本地 pipeline cache 锁文件无法创建，未影响测试执行与像素检查。
- git diff --check 通过。

未进行人工 GUI 操作验收、Release 构建、部署包验证或性能基准测量。
