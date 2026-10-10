# 第三方依赖

此目录只维护当前 MAT 读取/导出依赖。CMake 在 `ENABLE_MAT=ON` 时链接以下静态库，并使用对应目录的头文件：

| 目录 | 版本 | 链接文件 |
| --- | --- | --- |
| `matio-1.5.28/` | matio 1.5.28 | `lib/libmatio.a` |
| `hdf5-1.14.6/` | HDF5 1.14.6 | `lib/libhdf5.a` |
| `zlib131/` | zlib 1.3.1 | `lib/libzlibstatic.a` |

静态库须与项目 Qt 6.8.3 / LLVM-MinGW 17 工具链兼容。头文件、库及随发行包提供的配置元数据保持原有目录结构；`bin/` 中工具和动态库不属于当前应用的部署输入，部署运行时由 `tools/deploy_qt6.ps1` 收集。

QCustomPlot、Qt5 QuaZip 和 pybind11 已无构建/运行调用方，已移出工作目录；历史版本可从 Git 查询。当前绘图使用 Qt Quick Scene Graph，ZIP 读写使用现有实现，Python API 已遗弃。

构建与工具链说明见 [构建文档](../docs/dev/build.md)和[工具链排错](../docs/dev/toolchain-troubleshooting.md)。更新依赖时同步版本目录、CMake 链接/头文件路径及此表。
