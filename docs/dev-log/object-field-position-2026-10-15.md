# 对象字段增删位置修复

普通字段比较结果相同，QML 排序未保持原始顺序；同时数组模型刷新导致列表重建。增加原始位置作为排序次序，以稳定字段 ID 同步 ListModel 局部变化。回归覆盖 36 个字段的显示顺序、滚动后的追加、删除末尾和删除视口上方字段，验证可见行实例及屏幕位置保持。

验证：Debug 构建通过，qmllint 零 warning；Fusion/offscreen/software 下 objectdata_test、appcontroller_test、session_test、trajectory_test 共 4/4 通过。测试临时目录为 build_qt6-debug/test-temp。未执行手工 GUI、真实 GPU、Release 或部署验证。
