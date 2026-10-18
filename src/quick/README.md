# C++ 源码导航

| 目录 | 职责 | 主要入口 |
| --- | --- | --- |
| 根目录 | 启动、命令行与 QML 类型注册 | `main.cpp`、`startupfiles.*`、`qmltypes.h` |
| `controller/` | GUI 线程协调状态、任务、子图和会话 | `appcontroller.h`；实现按 loading/plots/trajectory/session/templates/objects/objectevaluation/image/about 分文件 |
| `automation/` | Agent/CLI 本机接入 | `automationserver.*`、`commandcatalog.*`、`automationtasks.cpp`、`automationcommands.cpp`：协议/schema、任务/幂等、检查计划 |
| `io/` | CSV/TXT/MAT/XLSX 解析、导出与工作线程 | `dataloadworker.*`、`dataexportworker.*`；`loadedtable.h`、`exporttable.h` 为共用数据结构 |
| `models/` | 信号树、信号元数据、对象定义与规则计算 | `signalmodel.*`、`signalmetadata.*`、`objectdefinition.*` |
| `state/` | 会话/模板值配置、当前格式校验与 JSON 原子读写 | `sessiondocument.*`、`viewconfiguration.*`、`viewtemplatedocument.*`、`jsonvalidation.h` |
| `items/` | Qt Quick 项、输入交互、游标及 Scene Graph 提交 | `plotitem.*`、`plotitem_cursor.cpp`、`plotitem_interaction.cpp`、`plotitem_render.cpp`、`trajectoryitem.*` |
| `render/` | Store、范围索引、LOD、几何、刻度和航迹投影 | 不依赖 QQuickItem；核心算法与 GUI 项分离 |

头文件搜索根为 `src/quick`。同目录包含本地文件名；跨目录用显式模块路径，例如 `#include "io/loadedtable.h"`。应用、测试和性能基准都遵循同一约定，不添加旧路径兼容头。

这些是同一应用内的职责分组，不引入新的运行时层或构建库。跨线程只传不可变快照，游标只查原始 Store；QML_FOREIGN 注册仍集中在根目录 `qmltypes.h`。CMake 的 `DI_*_SOURCES` 共用列表维护所有实际路径。

修改前参考[整体架构](../../docs/architecture/overview.md)、[构建](../../docs/dev/build.md)和[测试](../../docs/dev/testing.md)。历史开发日志中的旧平铺路径保留为当时记录。
