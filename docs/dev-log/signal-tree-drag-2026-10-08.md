# 信号树拖拽到子图（2026-10-08）

先以 7e3c175 提交信号树紧凑布局、样式统一和单行面包屑导航，再添加本功能。

从信号名称左键拖动超过 8 个逻辑像素后显示名称/颜色预览，按可见 QuickPlot 的实际坐标选择目标并复用 dropHighlighted 边框。松开调用 AppController::addSignalToPlot，校验源行与目标索引后激活目标，并复用 selectSignal 的时间图/航迹绑定规则。重复拖入是幂等添加，其他子图绑定保持；松开在图外或取消不添加。模型重置及拖动来源销毁时清理预览和目标，避免来源行号重排后误绑定。内部拖动不使用文件拖放 MIME 路径。

鼠标事件回归覆盖过滤后源索引、跨图添加、目标高亮、活动图切换、重复添加、图外取消和无效索引。Debug 构建通过，DataInspector_qmllint 零 warning，Fusion/offscreen 的 appcontroller_test、signalselection_test、session_test 三套 CTest 通过。测试 TEMP/TMP/TMPDIR 指向工作区 build_qt6-debug/test-temp。未进行人工 GUI、硬件 GPU 视觉、Release 或部署验收。

用户手感反馈后的最终实现保留名称/颜色卡片，保留信号树原行，卡片文字按原名称的按下位置跟随，不再从鼠标下方浮出。拖动期间屏蔽悬停提示，抓取控件及覆盖窗口的仅悬停 MouseArea 使用 BlankCursor；松开/取消后隐藏卡片并恢复指针。回归增加原行可见性、卡片起点与热点位置、空白指针及释放清理检查。

上下拖动误触列表滑动修复：信号名称的 MouseArea 从按下时就 preventStealing，不等拖动阈值达到后再设置；分组标题不受此规则影响。增加有长列表时先移动 4 像素再垂直拖动 30/60 像素的鼠标回归，检查 contentY 不变。

拖拽热区扩展：名称 MouseArea 对信号行覆盖复选框右侧的完整行高和宽度，包含上下空白与色条；名称卡片仍从名称原位开始，源行保留。色条单击继续打开属性，移动超过阈值后只执行拖拽。回归增加空白处及色条起拖，验证目标绑定与不误开属性；Debug 构建、qmllint 零 warning、Fusion/offscreen 拖拽及导航用例通过。未进行人工 GUI 或 Release 验证。
