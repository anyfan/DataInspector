# v0.3.0 发布验证（2026-10-15）

本版本包含跨分组同源信号按时间合并、对象字段稳定排序及局部列表更新，并接续此前源码目录整理。项目版本及更新记录为 0.3.0。会话保存为 v11，兼容读取 v1–v10；旧程序无法读取 v11。

Release 启用 MAT、关闭 GPU 测试。完整构建通过，DataInspector_qmllint 零 warning；Fusion/offscreen/software 下 CTest 12/12 通过（30.62 秒），报告为本地 build_qt6-release/release-v0.3.0-tests.xml。测试 TEMP/TMP 指向构建目录，避免沙箱系统临时目录权限问题。

本地 dist/v0.3.0/DataInspector 部署包通过移除 SDK 搜索路径后的 15 秒启动检查。此前 Debug 相关套件 4/4 通过，包含多来源合并/会话重算和滚动后字段增删保持位置的回归。未执行真实 GPU 或人工 GUI 验收。

正式包由 v0.3.0 标签触发 GitHub Actions，在干净 runner 重新完成 Release 构建、静态检查、全套测试和部署启动检查后发布 ZIP 与 SHA256 校验文件。
