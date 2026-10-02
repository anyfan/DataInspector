# 航迹 CAD 三轴旋转操纵器

日期：2026-10-02。

## 问题与最终操作

自由/X/Y/Z 下拉切换增加操作步骤。根据三轴球形旋转参考图，改为直接抓取式操纵器，移除模式选择框和可写 rotationAxis 属性。

- 绘图区中心显示红 X、绿 Y、蓝 Z 旋转环，前半环正常、后半环淡化，悬停/抓取高亮增粗。
- 左键直接拖彩色环约束对应世界轴，沿环跟随角度；侧视退化为切线拖动，端部提供投影基回退。
- 环内空白处自由旋转，环外或 Shift+左键平移；中键、Alt+左键和角落方向轴保留自由旋转。
- 按下后冻结手势，离开环不切换轴；角差累加支持多圈，反馈当前轴和累计角度。
- 环尺寸不受平移/缩放或数据范围影响，最多 82 逻辑像素半径，小子图缩小至短边 24%。中心与原投影支点保持一致。
- 绘制和命中共享已显示相机对应的不可变操纵器，跨线程整体替换快照；每个颜色/前后层合并节点，悬停不重复生成或上传航迹。
- 二维没有操纵器，左键平移，中键忽略；会话格式不变。

实现：`render/trajectorybuilder.*`、`trajectoryitem.*`、`TrajectoryPlot.qml`。同步更新用户、架构、并发及测试文档。

## 验证

- Debug 全目标构建和 `DataInspector_qmllint` 零警告通过。
- `trajectory_test`、`appcontroller_test`、`session_test`、`rendercore_test` 四套 CTest 通过。
- 四个预设视角 × 三条世界轴直接抓环回归通过，包含侧视退化、中心固定、比例不变和拖回起点。
- 环命中容差、固定大小、小视口有限几何、连续两圈、Shift 平移及二维行为回归通过。
- 源码/编译 QML 离屏窗口鼠标事件通过；编译 QML 在 Windows/D3D11 窗口的实际事件与截图检查通过。
- 直接运行 `trajectory_raster_test.exe`，D3D11 使用 Intel Iris Xe 硬件适配器，通过原视角像素、操纵器高亮和直接拖环绘制检查；X 环红色覆盖像素由 1424 增至 4924。
- 截图输出在 Debug 构建目录：`trajectory-cad-handles.png`、`trajectory-cad-gizmo.png`、`trajectory-cad-rotated.png`。
- 尚未人工长时间操作手感验收，未进行 Release 构建或部署。
