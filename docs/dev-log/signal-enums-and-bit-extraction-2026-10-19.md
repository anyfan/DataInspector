# 位提取与信号枚举编辑 · 2026-10-19

用户要求合并 bit 拆分和位段提取，在信号属性中编辑枚举，状态配置不使用弹窗。

## 最终实现

- 处理页仅显示一个“位提取（bit／位段）”类型，通过输出方式选择一条连续位段或逐 bit 多条结果，复用已有 bitfield/bits 数值语义。新单 bit 无符号结果与拆位结果默认 0 未生效／1 生效；其他位段不继承原状态字枚举。
- SignalPropertiesPane 嵌入 SignalBrowser，在左侧编辑信号名称、样式及整数／名称／颜色表格，颜色使用页内色板；原始、派生信号通用。返回和切换信号保留未应用草稿；数据来源结构变化清除失效草稿。
- 状态图仅组织来源、名称、顺序与显隐，新增／移除／移动和属性跳转均在侧栏。来源从信号树选择；重复添加恢复显示而不创建重复行。QuickPlot 不再创建 StateDisplayDialog；原信号属性 Dialog 也已替换。
- 枚举由 AppController 按信号持有，来源删除统一重映射，刷新状态图时复制进不可变配置。修改信号枚举同步所有引用的状态图；整数有效性仍在状态内核检查，位提取只在处理规则执行。
- 会话 v20 的信号条目必填 enums；模板 v5 的被引用信号保存枚举，匹配后应用并支持撤销。状态行 JSON 严格只接受 source/name/visible。没有旧格式迁移。

## 验证

Debug DataInspector 和相关测试目标构建通过；Release 使用既有 DATAINSPECTOR_BIN_DIR=bin-review 构建通过，随后恢复默认 bin。Debug/Release qmllint 均零警告。

appcontroller_test、session_test、objectdata_test、automation_test、trajectory_test 五套 CTest 全部通过，总计 40.94 秒。覆盖共享枚举更新、严格校验、最近原始样本、侧栏草稿、状态行入口、会话／模板及来源删除重映射。处理页旧测试已改为显式选择批量 bit 输出，仍检查原来的八条输出与数值行为，并增加默认枚举检查。

Windows D3D11 的 signalEnumsInlineEditorAndSharedViews 测试通过，已检查 signal-enum-editor-gpu.png 的侧栏编辑布局；这不代表用户真实数据的手动验收。首次 Release 链接因原 exe 被占用而失败，期间未关闭用户窗口，改为 bin-review 完成验证。随后占用已解除，默认 bin/DataInspector.exe 也成功构建更新；另保留 bin/DataInspector-state-edit.exe 供查看。

## 属性面板移至右侧

按用户截图，将 SignalPropertiesPane 从 SignalBrowser 内移到 Main 横向 SplitView 最右侧，默认宽 320、最小 260 逻辑像素，可拖动分隔条调宽。左侧树保持原搜索／滚动位置；关闭属性面板后中心绘图区恢复宽度。状态行管理使用同一右侧面板，选择来源仍到左侧树，进入位提取仍转到处理页。SignalBrowser 通过显式属性引用协调同一个编辑器，不重复创建草稿与表格。面板头部提供关闭按钮，从状态行进入属性时显示返回按钮。

验证：Debug、Release 和两者 qmllint 通过；appcontroller_test/session_test/objectdata_test 三套 CTest 3/3 通过（57.25 秒）。混合状态／曲线布局测试检查面板位于图面右侧、左树保留且图面可见；Windows D3D11 测试通过（1.98 秒），已检查 right-properties-gpu.png。原 Release 进程仍运行，未关闭，使用 bin-review 输出并恢复默认 bin；新版复制为 bin/DataInspector-right-properties.exe。

## 属性跟随信号及菜单清理

- 属性面板打开时，直接点击信号名称、勾选框或右键选中其他信号会跟随切换；关闭后点击名称不主动打开。来源绑定、状态选择和拖拽保留优先级，勾选仍控制原来的显示／绑定。
- 重命名、恢复原名称和原始信号时间偏移合并到信号属性，绝对秒偏移不重复累加；派生信号显示随输入来源时间变化。文件／分组批量偏移也在右侧内嵌编辑，保留混合值提示、重置和取消未提交的行为，不再弹窗。
- configureSignalProperties 先验证整页名称、样式、枚举和时间偏移，验证错误不部分提交。草稿仅在相对已应用快照有修改时缓存，避免无修改的旧面板内容盖掉外部重命名和批量偏移。
- 移除信号右键菜单“添加到状态图”和“绑定到对象”，以及独立的重命名／时间偏移菜单项；保留信号属性和派生结果的处理规则入口。状态图从“状态行…”选择来源，对象字段在对象页绑定。
- 验证：Debug/Release DataInspector 与两者 qmllint 通过；appcontroller_test、session_test、objectdata_test 3/3 通过（27.39 秒）。真实鼠标回归覆盖名称点击切换、勾选切换及显示语义、草稿恢复、绝对偏移重复应用、混合范围重置、恢复原名称和无效组合提交不部分修改。Windows D3D11 对应测试通过（1.286 秒），出现受限缓存目录的 pipeline cache lock 文件警告，不影响测试通过；未做用户真实数据手动验收。
- Release 使用 bin-review 构建并恢复默认 bin 配置；可运行的新副本为 bin/DataInspector-properties-sync.exe，未关闭用户窗口。
