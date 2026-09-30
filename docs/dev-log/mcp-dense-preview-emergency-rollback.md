# 密集预览修复的紧急回退

2026-09-30

用户报告提交 `9aa5769` 后普通航向和俯仰曲线在约 400 秒处的跳变连线消失，局部曲线出现断裂。此前密集波形的边缘一致性验证不足以覆盖普通曲线的连续性，不能据此认定绘图功能无回归。

紧急处置：完整撤回该提交的渲染、LOD 和相关改动，恢复 `a8f062f` 的实现，不改写 Git 历史。原始的密集预览边缘小凸点暂时保留，优先恢复正常绘图。

增加 `rendercore_test::lodGeometryPreservesStepConnections`，检查密集采样的上升/下降阶跃、桶边界附近三种跳变位置，并检查跳变线段 10%、50%、90% 处均有几何覆盖。这是恢复版的基本连续性防护，不代替用户截图数据的完整 GUI 验证。

验证：渲染和 LOD 的三个源码文件与 `a8f062f` 无差异；Debug 全构建、qmllint、rendercore_test、plotitem_lod_test、appcontroller_test，以及现有 GPU raster/blend 测试通过。尚未验证截图对应的原始数据，也未构建 Release 或更新部署包。

后续原始数据复核：用户确认文件为 `test_file/发动机负载测试.mat`，`p1` 的 `INS_FO_data_f_thdg` 与 `INS_FO_data_f_pitch`。共 17432 点，时间 301.64–998.88 秒；两条信号没有 NaN/Inf。397.72→397.76 秒，航向 328.09030151→52.06779861 度，俯仰 0.79839998→1.00650001 度。

把 MAT 前三列无损导出为测试 CSV，以 `plotitem_raster_test --step-csv build_qt6-debug/engine-attitude.csv` 运行真实 GPU 绘制，逐一检查两条跳变线段中间 10%–90% 的 162 个探测位置，缺失为 0。捕获图像确认两条跳变均完整连接，俯仰前段无截图中的断裂。默认光栅测试也再次通过。图像保存于构建目录 `engine-attitude-restored.png`，原始数据及生成图不纳入 Git。未做完整应用人工交互验收或 Release 验证。
