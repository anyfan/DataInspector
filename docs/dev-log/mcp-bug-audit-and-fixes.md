# 数据导入与绘图数据仓库：缺陷审查及修复

## 范围与基线

- 基线提交：`24d206d`，分支 `codex/qt-quick-renderer`；开始检查时工作区无未提交修改。
- 本轮重点审查 `src/quick/dataloadworker.cpp`、`src/quick/render/plotseriesstore.cpp`，并检查控制器加载/导出、线程生命周期、LOD 调度及现有测试组织。
- 修改前 Release 构建成功，原有 CTest 5/5 套件通过。边界缺陷未被原套件覆盖。
- 这是有针对性的代码审查与回归修复，不是对整个程序不存在其他缺陷的保证。

## 已修复

### 1. MAT 非普通 double 存储被当作连续数组读取（高风险）

位置：`src/quick/dataloadworker.cpp`，`buildMatTable`。

旧实现仅检查数值类型与二维形状。复数的 `data` 指向实部/虚部描述结构，稀疏矩阵的 `data` 指向稀疏结构；即使数值类型为 `MAT_T_DOUBLE`，也不能转换为普通 `double*` 遍历。错误转换可能造成越界读取、崩溃或错误数据。

修复：要求普通实数 `MAT_C_DOUBLE`，检查数据指针、非空维度、当前 int 索引容量、大小乘法溢出及实际缓冲区长度。复数、稀疏和空矩阵跳过，文件中其他有效 pN 表继续读取。不自动丢弃复数虚部或展开稀疏矩阵。

回归：`tests/dataloadworker_test.cpp` 中的复数、稀疏、空矩阵三组 MAT 文件，每组均附带合法压缩 double 表，验证仅合法表被加载且数值完整。没有在旧实现上执行可能触发越界读取的复数/稀疏样例。

### 2. CSV 错误引号与数字解析不一致（数据正确性）

位置：`src/quick/dataloadworker.cpp`。

- 未闭合引号、未引用字段内部引号、闭合引号后紧跟文本，旧实现可能移除引号后接受错误值。
- 无引号路径用 `QLocale::c()`，可接受分组逗号；有引号路径用 `QString::toDouble()`，拒绝分组逗号。同一数值是否加引号会改变结果。

修复：明确区分引用中、引用结束和普通字段；非法引用行整行跳过，非法表头报错。保留双引号转义、字段外空白、UTF-8 BOM。统一两条路径的 C locale 数值解析，拒绝分组分隔符；保留信号非法值转 NaN、非法时间文本跳过的语义。合并两份重复的入库和时间范围更新逻辑，降低后续行为分歧风险。仍不支持跨行引用字段。

兼容性说明：分号/制表符 CSV 中的 `1,234` 不再作为 1234 接受；应使用 `1234`。加引号与不加引号的行为一致。

### 3. 点数组输入遗漏单调性检测（游标与范围正确性）

位置：`src/quick/render/plotseriesstore.cpp`，`makeSeriesData`。

旧实现只对分列输入检查时间顺序；点数组输入即使 `monotonicTimeKnown=false` 也直接信任默认值。乱序或含无效时间时，后续二分查询和端点范围计算会使用错误前提。

修复：两种输入统一执行单调性检测；已由调用方确认的元数据仍走快捷路径，检测到首次乱序或非有限时间即停止扫描。

回归：验证乱序及含 NaN 时间的点数组，检查时间范围、最近样本与窗口 Y 范围。

### 4. 相同时间偏移造成快照与 LOD 无效化（优化）

位置：`src/quick/render/plotseriesstore.cpp`，`addTimeOffset`。

修复：只有实际变化的序列才校验并替换快照；整批无变化时保留 generation。混合批次仅替换变化项，保留原有批次预校验与旧快照不变的约束，避免重复设置偏移时无谓触发 LOD 数据变化处理。

回归：重复绝对偏移、零增量、混合已变/未变序列，以及旧快照仍可读取。

## 测试组织

- 新增独立 `tests/dataloadworker_test.cpp`，通过 `CMakeLists.txt` 注册 `dataloadworker_test`，不继续扩大控制器集成测试文件。
- `tests/rendercore_test.cpp` 补充点数组与无变化偏移回归。
- 测试数据使用临时目录生成，不修改用户数据或导入样本。

## 验证结果

环境：Windows 11，Qt 6.8.3 LLVM-MinGW；Release/Debug 均为 `ENABLE_MAT=ON`，`ENABLE_GPU_TESTS=OFF`。

- 修复前新增回归：CSV 三项、数据仓库两项均失败，确认不是只增加永远通过的测试。
- Release：`cmake --build build_qt6-release --parallel 4` 成功；`ctest --test-dir build_qt6-release --output-on-failure` 为 **6/6 套件通过**。
- Release 单独复核 `dataloadworker_test`：三个 CSV 用例、三个 MAT 数据行均通过，含初始化/清理共 8 passed、0 failed、0 skipped。
- Debug：`cmake --build build_qt6-debug --parallel 2` 成功；`ctest --test-dir build_qt6-debug --output-on-failure` 为 **6/6 套件通过**。
- `get_diagnostics`：本次查询未返回 error/warning；以实际构建和测试结果作为主要验证依据。
- `git diff --check`：无空白错误；Git 提示 CMakeLists.txt 后续可能受 autocrlf 转换，这是换行配置提示，不是编译/测试失败。
- README 已同步 CSV 数值约定及 MAT 支持边界；本轮不自动生成 Git 提交。

## 边界与后续建议

- 本轮不改变 UI 布局、绘图几何算法、文件导出格式，也不进行大规模架构重构。
- 建议后续单独检查 MAT 大文件取消响应及标题字符串解析的边界处理，并加入损坏文件/资源上限测试。
- GPU 真机交互、长时间运行、超大数据集吞吐和内存峰值仍需专门验证；本轮不把代码路径优化量化为已测得的帧率或内存收益。
