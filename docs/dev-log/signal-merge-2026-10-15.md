# 同源采样合并

新增对象派生 merge 规则与多输入勾选界面，支持跨分组字段合并、时间偏移、冲突检测和会话 v11 恢复。objectdata_test 新增乱序、交错、重复、缺失、冲突、偏移、JSON 往返和取消回归。

验证：Debug 构建通过；DataInspector_qmllint 零 warning；Fusion/offscreen/software 下 appcontroller_test、session_test、objectdata_test、trajectory_test 共 4/4 通过。沙箱默认临时目录写入失败后，设置 TEMP/TMP 为 build_qt6-debug/test-temp 完成验证。未执行手工 GUI、真实 GPU、Release 或部署验证。
