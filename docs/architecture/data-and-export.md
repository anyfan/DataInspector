# 数据加载与导出

> **状态**：现行 · **读者**：AI 代理 / 开发者 · **关联代码**：`src/quick/dataloadworker.*`、`xlsxreader/writer.*`、`matwriter.*`、`mat5streamwriter.*`、`dataexportworker.*`、`exportvalidation.*`、`startupfiles.*` · **配套测试**：`dataloadworker_test`、`xlsxwriter_test`、`matwriter_test`、`startupfiles_test`

## 加载管线

`DataLoadWorker` 在后台串行执行：

1. 按文件依次解析 CSV/TXT/XLSX/MAT。
2. 每张表构建 `LoadedTable`（值类型，注册到 Qt 元类型系统以便跨线程传递）。
3. 加载线程同步构建分块极值索引（`PlotRangeIndex`），不在 GUI 线程逐点建索引。
4. 新文件以增量方式加入 `PlotSeriesStore`，不重建已加载曲线。
5. 进度条显示批量总进度；可通过取消按钮停止。

### CSV 解析要点

- 自动识别逗号/分号/制表符；单行引号字段、`""` 转义、UTF-8 BOM 支持。
- 非法引号行整行跳过；表头引号错误中止导入。
- 数值统一 C locale，拒绝千位分组符。
- 时间无法解析的行跳过；信号无法解析记 `NaN`。

### MAT 解析要点

- 只接受普通实数 `MAT_C_DOUBLE` 稠密二维矩阵；复数、稀疏、空矩阵跳过。
- 检查数据指针、维度、int 索引容量、乘法溢出和实际缓冲区长度。
- `pN_title` / `pN_title2` 按列主序字符矩阵解析为列名。

## 来源隔离

- 每个系列记录规范化完整路径 `sourceFile` 和表序号 `sourceTable`。
- 显示名称不决定来源身份；同名文件在信号树显示为 `same.csv`、`same.csv [2]`。
- 导出分组键 = `(sourceFile, sourceTable, timeOffset)`，不用字符串拼接。
- 删除某个标签只移除对应来源；文件级时间偏移不作用于同名的另一个来源。

## 导出

### PNG 图片

`AppController::exportPlotImage` 接收绘图区 QQuickItem，等待当前时间图 LOD 与可见航迹任务完成后调用 `grabToImage`；QML 导出当前子图或暂时以平铺布局显示全部子图。抓取包括轴、图例、游标及航迹元素，不含工具栏和信号树。默认 2 倍尺寸，每边最多 8192 像素，超限拒绝。抓取后不可变 QImage 值副本投递给 DataExportWorker，在后台编码 PNG 并通过 `QSaveFile` 原子写入；取消保留旧文件，同一时间不允许其他导出/加载/恢复任务。捕获期间禁用绘图区和工具栏的交互，Scene Graph 抓取由 Qt 管理；单次 PNG 编码不能被中途打断，完成编码后提交前再次检查取消。

`DataExportWorker` 后台调度，与加载共用工作线程队列。

### 共用结构

`exporttable.h` 定义 `DataExportSeries` / `DataExportTable`，XLSX 与 MAT 写入器共用。

### 时间基校验

导出前 `exportvalidation.cpp::validateExportTimeBases`：

- 同表系列样本数必须相同。
- 共享同一不可变时间向量且偏移相同的列直接通过。
- 其他情况逐样本比较偏移后的时间（不用模糊比较，配对 NaN 视为同位置缺失）。
- 不匹配则报错，不套用第一列时间，不覆盖已有文件。
- 长比较每 4096 行检查一次取消。

### XLSX 写入

- `xlsxwriter.*` 写多工作表；单表超 1,048,575 行自动拆分。
- `NaN` 写空单元格；ZIP 压缩可关。
- `QSaveFile` 原子保存。

### MAT 流式写入

- `matwriter.*` 规划导出表；`mat5streamwriter.*` 负责 Level 5 帧编码。
- 数值列按列主序、单块 8192 个 double（64 KiB）流式写入，不分配整表数值缓冲。
- 直接写 `QSaveFile` 临时文件，取消或失败时不替换原文件。
- 块间、表间、提交前检查取消。
- `pN` 编号：原 MAT 表保留编号，其他表取最小空闲编号；只写 `pN` 与 `pN_title`。
- 单变量 2 GB 上限由 framing 模块校验。

## 启动参数

`startupfiles.*` 纯解析单元：

- 从 `argv[1..]` 提取数据文件与会话文件；跳过 Qt 开关及其取值。
- 相对路径按工作目录解析；按 canonical 路径去重。
- 会话文件优先，同时传入的散装数据文件被忽略。
- `main.cpp` 在 QML 根对象创建后用 `QTimer::singleShot(0, ...)` 投递到事件循环。

## 航迹绘制来源导出

绘制信号范围收集全部子图中可见航迹绑定的位置及已启用姿态来源，以 Store 系列 ID 去重；候选但未绑定、隐藏航迹、关闭姿态的姿态来源不单独贡献。多条航迹复用来源不会重复导出列。读取原始 Store 值，保持纬度/经度/记录高度、姿态单位和原始时间，派生 NED 及模型几何不参与导出。来源分组和同表严格时间基校验沿用现有规则。
