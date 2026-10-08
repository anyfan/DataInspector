# 构建自动并行（2026-10-08）

Debug/Release 的 CMake buildPresets 不再固定 jobs=4，改由预设环境 CMAKE_BUILD_PARALLEL_LEVEL=$penv{NUMBER_OF_PROCESSORS} 使用 Windows 逻辑 CPU 数。本机环境与 .NET ProcessorCount 均为 20，通过 cmake --build --preset qt6-clang-debug 实际构建验证；可用命令行 --parallel N 手动覆盖。更新构建和测试文档中的预设命令。

本次验证 Debug，不将并行任务数等同于固定的耗时提升；编译缓存、链接、磁盘和内存同样影响耗时。Release 预设采用相同配置，未执行 Release 构建。
