# 并发与线程安全

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联代码**：`src/quick/plotitem.*`、`render/`、`appcontroller_*.cpp` · **改动任何跨线程共享数据前必读**

## 线程划分

| 线程 | 职责 |
| --- | --- |
| GUI 主线程 | QML 事件、`PlotItem` 视图交互、游标读数 |
| Scene Graph 渲染线程 | `updatePaintNode` 同步和节点提交、渲染线程拥有的 QSG 节点 |
| 加载/导出工作线程 | `DataLoadWorker` 解析文件、构建 `PlotRangeIndex`；`DataExportWorker` 写 XLSX/MAT |
| LOD 后台线程（最多 2 个） | `PlotLodScheduler` 生成结构化 LOD |

## 不可变快照

跨线程共享的数据一律不可变：

- `PlotSeriesStore` 持有不可变原始序列和索引；画笔/偏移变化时整体替换某系列的快照，旧快照不释放。
- 游标查询始终访问原始快照，不用 LOD、不插值；快照替换后旧快照仍可安全读取。
- 导出从 Store 取不可变快照后台写盘。

## PlotItem 的锁

`PlotItem` 用 `m_dataMutex` 保护视图/游标状态，因为以下调用可能并发：

- `setRange`（来自 `AppController::applySharedXRange`，在其他子图的 `rangeChanged` 处理器中推入）
- `setCursorX`
- LOD 调度器回调
- 输入事件（mousePress/mouseMove/wheel）

关键约束：

- `mousePressEvent` 的拖动起始快照必须在锁内完成，不能在释放锁后再读 `m_xMinimum` 等成员。
- `mouseMoveEvent` 比较 `m_cursorMode` 时必须走已加锁的 `cursorMode()` 访问器，不能裸读成员。

## 会话恢复的事务性

恢复期间：

- 控制器入口拒绝并行的加载、导出、保存、视图修改。
- 新数据先暂存，全部文件加载和结构校验通过后才一次性切换 Store/模型/布局/绑定。
- 切换失败丢弃暂存数据，保留旧会话。
- 内存峰值 = 旧数据 + 新加载数据，需预留。

## 取消与原子写

- 导出/加载进度循环定期检查取消标志。
- 文件写入用 `QSaveFile`：先写临时文件，完整成功后才替换目标；取消时保留原文件。
- 单次 OS 写入或最终提交不能被强行中断。

## 轨迹后台预览

TrajectoryItem 另有最多两个工作线程的进程内线程池。每项最多一个任务，后续请求合并为最新修订；任务定期检查原子取消标志，GUI 定时器通过 acquire/release 的 done 标志接收结果。不捕获 GUI 对象指针，销毁视图只取消任务，任务可以安全结束。

仅相机变化不会反复取消任务：先显示已完成且来源匹配的预览，再生成最新相机结果，避免连续拖动饿死画面。来源改变或销毁才取消旧任务，旧来源结果不能提交。中间结果可以复用已准备的空间索引；标记仍使用对应已显示的相机。拖动结束再请求精细预览。

渲染线程只读取在 m_mutex 下获得的不可变 TrajectoryPreview 指针；数据/相机拖动快照同样在锁内获取。游标移动更新原始位置标记，通常不重新生成轨迹几何；标签占用空间引起视窗尺寸变化时会重新生成投影。
