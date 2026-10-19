# 位提取与公式多个输出 · 2026-10-19

用户确认位提取参数为“起始 bit、位数”：b0～b3 填 0、4，单独 b1 填 1、1。位提取与自定义公式均需一条规则的多个输出，字宽自动识别、默认小端。

## 最终行为

- 位提取和公式使用 1–32 行输出列表，各行有名称和独立位范围／公式，可增删。逐 bit 的批量拆分方式继续保留，切换为输出列表时保留每个 bit 的身份及参数。
- 位提取默认自动字宽，后台扫描有效非负整数及全部输出的所需范围，选最小 8/16/32 位；摘要显示计算后的有效字宽。此结果不是源文件真实类型。默认小端直接按数值取位；大端先按有效字宽交换字节，再按同一 bit 编号提取。每行可选择二补码有符号解释。
- 公式共享已绑定的 x/y/z 和严格相同的时间基，每行单独编译与计算，逻辑表达式各自带阶梯标记。语法错误应用前拒绝；单个表达式产生非有限值只清空该输出的对应采样，不破坏其他结果。
- 编辑按输出 ID 保留系列、样式及枚举；新输出追加，删除输出统一清理视图和重映射。若下游仍引用待删输出，整次提交拒绝并提示先解除输入，不隐式删除其他规则。各输出定义、自动字宽和端序进入增量缓存键，沿用取消与 512 MiB 值预算。
- 当前会话 v21 显式保存公共 autoWordBits/byteOrder 和各输出 startBit/bitCount/signedField/expression；只接受当前格式，不迁移旧会话。模板仍为 v5。CLI rule_update schema 同步开放输出列表、自动字宽和端序。

## 验证

Debug DataInspector 及相关测试目标、Release DataInspector 和两者 qmllint 通过。appcontroller_test/session_test/objectdata_test/automation_test/trajectory_test 5/5 全部通过（41.63 秒）。新增测试覆盖多位段、小端／字节翻转、符号扩展、自动字宽、多公式逻辑阶梯、独立无效采样、严格定义、编辑增删及引用保护、样式身份保留、会话恢复和编辑器提交。

Windows D3D11 的 multipleOutputEditorQml 通过（0.897 秒），已检查 multi-output-formulas-gpu.png。界面测试依附 QQuickWindow，以保持编辑器来源选择状态有效；现有 Main 中规则输入测试通过模型对象修改新输出公式，源码与资源加载两种方式均通过。未做用户真实大数据的手动验收。

Release 在 bin-review 构建后恢复默认 bin 配置，新版可运行副本为 bin/DataInspector-multi-output.exe；保留用户已打开的窗口。

## 编辑区调整

移除位提取方式选择框及公共范围控件，统一输出列表：每行独立配置起始 bit、位数；单 bit 也是位数 1。已有 bits 规则进入编辑时展开成各行定义，提交使用 bitfield。公式框新增“表达式”标签、白色/主题背景、边框、内边距和焦点高亮，底部补充比例偏置、差值绝对值、三轴模长、条件判断及限幅示例。

本次 Debug/Release 构建与 qmllint 零警告通过；objectdata_test 通过（20.80 秒），覆盖统一输出列表生成八条独立 bit 的原有完整界面流程。Windows D3D11 multipleOutputEditorQml 通过（3.490 秒），已查看 output-editor-gpu.png 确认输入框边界。未进行用户真实数据的手动验收。可运行副本：bin/DataInspector-output-editor.exe。

## 整理提交验证

整理时修复处理配置复制遗漏逐输出定义的问题：位提取／公式复制完整输出列表，保留各行位范围、公式与信号枚举，生成新输出 ID 并重绑副本内的下游引用。回归验证多位段、多公式、自定义枚举与下游数值结果一致。

最终 Debug 全部构建目标、Release DataInspector、两者 DataInspector_qmllint 均通过。设置 Fusion 与可写 TEMP/TMP 后，Debug CTest 14/14 全部通过（124.08 秒），包含实际进程的 automation_cli_test；git diff --check 与暂存区检查通过。本轮不重复真实数据手动验收，也不声称完整 GPU 光栅回归。Release 标准输出 bin/DataInspector.exe 已更新。
