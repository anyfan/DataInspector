# 并发与线程安全

## 线程划分

| 线程 | 职责 |
| --- | --- |
| GUI 主线程 | QML 事件、`PlotItem` 视图交互、`updatePaintNode` 提交、游标读数 |
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
