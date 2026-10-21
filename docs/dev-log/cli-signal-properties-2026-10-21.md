# CLI 信号属性适配

新增 signal_enums、set_signal_enums、signal_properties，复用现行共享枚举和属性控制器接口。支持枚举读取、整表更新、清空及属性原子提交，不改变会话格式。

automation_cli_test 增加真实应用和 Python CLI 的读写、幂等、非法枚举无部分修改、清空、属性更新及会话恢复回归。

对照控制器接口继续审计 CLI：修复 object_create 帮助声明 processing 而运行时拒绝的问题，补齐处理组/对象及字段管理、规则创建/删除和输入发现、航迹样式/选择/移除、状态行、视图模式、曲线坐标、恢复原名、范围偏移和卸载文件。rule_create 复用 rule_update schema 和派生任务结果检查。用户指南列出现有覆盖和仍未开放的 GUI 功能，避免将本轮适配描述为覆盖全部操作。
