# 信号重命名、MAT 导出与线型预览单击编辑

## 背景

用户提出三项改动：

1. 信号树中加载的信号需要右键“重命名”。
2. 导出功能需要增加 MAT 格式，且布局与导入的 MAT 文件一致。
3. 信号树线型预览上的“双击编辑信号线属性”提示去掉，改为单击直接弹出信号属性窗口。

## 导入 MAT 的实际布局（`test_file/*.mat` 探查结果）

- MATLAB Level 5（`MATLAB 5.0 MAT-file`），小端，所有变量均为未压缩 `miMATRIX`。
- `pN`：`double` 二维矩阵，列主序，`dims = (行数, 1 + 信号数)`，第 0 列为时间。
- `pN_title`：`char` 矩阵，`data_type = miUTF8`，`dims = (1 + 信号数, 最长字符数)`，
  每行一个名称，第 0 行是时间列标题，短名称以空格补齐；字符按列主序连续写入，
  多字节 UTF-8 字符不定长，`readMatStrings()` 正是按此规则逐字符分配到各行。
- `pN_title2`：可选，同样布局；加载时名称 = `title2 + " " + title1`。

导出只写 `pN` 与 `pN_title`（第 0 行固定为 `Time`，其余行为当前显示名称，含重命名结果）。
显示名称已是合成后的单个字符串，无法可靠拆回 `title1/title2`，因此不再写 `pN_title2`；
加载器把 `title2` 视为可选，重新导入得到的名称与导出前完全一致。

## 改动

### `src/quick/exporttable.h`（新增）

- 新增 `DataExportSeries` / `DataExportTable`，作为 Excel 与 MAT 导出共用的输入结构。
- `src/quick/xlsxwriter.h` 中的 `XlsxExportSeries` / `XlsxExportTable` 改为别名，原有调用与测试不受影响。

### `src/quick/matwriter.h` / `src/quick/matwriter.cpp`（新增）

- `writeMatFile(path, tables, reportProgress, isCancelled)`：
  - 使用 matio `Mat_CreateVer(..., MAT_FT_MAT5)` 写 Level 5 文件，变量 `MAT_COMPRESSION_NONE`，与导入文件一致。
  - 每个表先按列主序填充 `time + 各信号` 的连续 `double` 缓冲（时间取第一条信号的时间基，
    行索引越界或非有限值写 `NaN`），再以 `MAT_F_DONT_COPY_DATA` 创建 `pN` 并写入。
  - `pN_title` 通过 `encodeCharMatrix()` 生成：按 UCS-4 码点补空格到最长名称，逐列逐行输出 UTF-8，
    以 `MAT_C_CHAR + MAT_T_UTF8` 创建（matio 1.5.28 会自行统计 UTF-8 字节数并原样写出）。
  - `pN` 编号：来源表名末段匹配 `^p(\d+)$` 且未被占用时沿用原编号（MAT 再导出保持变量名），
    其余表依次取最小空闲编号。
  - 单变量超过 MAT 5 的 2 GB 字节上限时直接报错；空表跳过；全部为空时报“没有可导出的数据”。
  - 进度：缓冲填充占每表 60%，写入后补齐；随后拷贝到 `QSaveFile` 占 95–99%，提交后 100%。
    先写入输出目录下的临时目录 `.datainspector-export-XXXXXX/export.mat`，成功后才替换目标文件；
    取消检查贯穿填充、表间与拷贝阶段。
- `matExportSupported()`：编译期 `ENABLE_MAT` 开关；关闭时 `writeMatFile()` 返回“当前版本未启用 MAT 支持”。

### `src/quick/dataexportworker.*`

- 新增槽 `exportMat(path, tables)`，与 `exportWorkbook` 共用 `progress` / `finished` 信号和取消标志。

### `src/quick/appcontroller.*`

- 导出表收集逻辑抽出为 `collectExportTables(scope, &tables)`，`beginExport(kind)` 统一进度/状态初始化，
  `m_exportKind` 使完成状态显示为“已导出 Excel：…”或“已导出 MAT：…”。
- 新增 `Q_INVOKABLE bool exportMat(const QVariant &filePath, int scope)`：自动补 `.mat` 后缀，
  未启用 MAT 时给出状态提示并返回 `false`。
- 新增 `Q_PROPERTY(bool matExportSupported)` 供 QML 决定是否显示 MAT 菜单项。
- 新增 `Q_INVOKABLE bool renameSignal(int row, const QString &name)`：调用模型重命名成功且名称实际变化时
  递增 `plotStateRevision`（图例按该修订号重建条目，与线条属性修改同一路径）并更新状态栏。

### `src/quick/signalmodel.*`

- 新增 `bool renameSignal(int row, const QString &name)`：去首尾空白，空名或行号非法返回 `false`；
  无过滤时只发 `dataChanged({DisplayRole, NameRole})`，有搜索过滤时重建可见节点（过滤按名称匹配）。

### `qml/Main.qml`

- 新增 `signalContextMenu`（“重命名…”“线条属性…”）与 `renameSignalDialog`（预填原名并全选，
  空白名称时禁用 OK，回车即确认）。信号树 `TapHandler` 右键现在对文件节点弹出移除菜单，
  对信号行弹出信号菜单；数据表分组节点保持无右键菜单。
- 线型预览 `MouseArea`：`onDoubleClicked` 改为 `onClicked`，移除 `ToolTip` 与 `hoverEnabled`，
  增加 `cursorShape: Qt.PointingHandCursor`，单击同时选中该行。
- 导出按钮提示改为“导出数据”，菜单项改为 “Excel · 全部已加载数据 / Excel · 当前所有子图已绘制的信号 /
  MAT · 全部已加载数据 / MAT · 当前所有子图已绘制的信号 / ZIP 压缩（Excel）”；MAT 两项及其分隔线
  仅在 `appController.matExportSupported` 为真时显示。新增 `exportMatDialog`（`*.mat`，默认后缀 `mat`）。

### `CMakeLists.txt`

- 新增 `DI_MAT_SOURCES`（matwriter）并加入 `DI_CONTROLLER_SOURCES`；`exporttable.h` 加入 `DI_XLSX_SOURCES`。

### `tests/appcontroller_test.cpp`

- `renamingSignalUpdatesModelLegendsAndExport`：空名/非法行被拒绝且不触发修订；重命名裁剪空白、
  触发一次 `plotBindingsChanged`、同名重命名不再触发；模型 `NameRole` 与搜索过滤跟随新名；Excel 导出表头使用新名。
- `matExportMirrorsImportLayout`：用 matio 直接检查导出文件——`p1` 为 `MAT_C_DOUBLE` 2×3、时间列在前、
  `NaN` 保留；`p1_title` 为 `MAT_C_CHAR + MAT_T_UTF8`、3 行、列数等于最长名称 `B 俯仰角` 的字符数；
  再用 `AppController` 重新加载，信号名（含中文重命名）与 `文件/pN` 分组完全还原。
- `matExportKeepsSourceTableNumbers`：加载含 `p3`、`p1` 的 MAT 与一个 CSV 后导出，
  变量为 `p1/p2/p3`（CSV 取空闲的 `p2`），缺失的 `.mat` 后缀自动补齐。

## 注意事项

- MAT 5 单变量上限 2 GB、整文件建议不超过 2 GB（MATLAB 对 v5 的限制）；超出时应改用 v7.3（HDF5），当前未实现。
- 同一导出表内的所有信号按第一条信号的行索引对齐（与 Excel 导出一致）；同名不同目录的文件会合并到同一分组，
  行数不同的信号以 `NaN` 补齐。
- `pN_title` 第 0 行固定写 `Time`，导入时会被 `composeMatSignalNames()` 忽略。
- 重命名只影响显示名称与导出表头，不改变源文件。

## 验证

- `cmake --build --preset qt6-clang-debug`：成功，无新增警告。
- `ctest`（build_qt6-debug）：5/5 通过；`appcontroller_test` 新增 3 个用例全部执行（未跳过）。
