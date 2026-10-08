# 信号树展开及滚动稳定性（2026-10-08）

展开/折叠的整模型 reset 会重建 ListView 的可见项，丢失浏览位置；导航显隐改变 anchors.topMargin，又改变视窗和滚动条轨道高度，拖动顶部滑块时产生反馈跳动。

SignalModel::toggleGroup 比较展开前后的节点，仅发出受影响区间的 rowsRemoved/rowsInserted 和 ExpandedRole 通知，保留其他节点的持久索引。检查全部祖先的展开状态，防止折叠外层后深层组仍然出现。重新计算祖先行索引。

点击组标题时取消惯性运动，记录该标题相对于视窗的位置，局部更新后 forceLayout 再恢复锚点并裁剪到合法滚动边界，避免 ListView 内部 originY 变化造成偏移。列表在内容边界停止，不做弹性越界滚动。

面包屑作为固定视窗上的覆盖导航，不再改变列表及滑块的高度/起点；滚动经过顶部时，导航覆盖已滚出或顶部部分行，正常滚动区域不重排。行数变化只使路径缓存失效，不先隐藏再显示导航。

回归覆盖模型通知/持久索引/深层折叠、浏览中点击组标题的坐标和 contentY 保持、顶部滑块拖动时的几何不变及单向滚动。Debug 构建、DataInspector_qmllint 零 warning、Fusion/offscreen 的 appcontroller_test/signalselection_test/session_test 三套 CTest 通过。未进行人工 GUI、硬件 GPU 视觉、Release 或部署验收。

导航后续修复：整条导航用 MouseArea 截断底层点击，路径段由自己的 MouseArea 接收单击/双击，不再用被动 TapHandler 将同一次点击传给底层控件。顶部对齐的非文件组被导航覆盖时，将该组加入当前路径，避免点击 pN 后仅剩文件名。宽度分配优先保留分组段，文件段先省略。回归覆盖点击分组后完整路径、空白导航点击不打开属性和超长文件名下 p19 完整显示。
