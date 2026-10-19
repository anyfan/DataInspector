# 会话保存与恢复

> **状态**：现行 · **关联代码**：`sessiondocument.*`、`viewconfiguration.*`、`viewtemplatedocument.*` · **改动格式必须同步本页**

## 使用与支持范围

“会话 → 保存会话…”（Ctrl+S）保存 `.disession`；“恢复会话…”（Ctrl+Shift+O）打开已有会话。文件为 JSON，不复制原始数据。当前只支持 `format = "DataInspectorSession"`、`version = 22`；旧版本及未来版本明确拒绝读取，不执行迁移。已有旧文件保持原样。

## 保存内容与校验

- `files`：原始文件路径，优先相对会话所在目录，最多 10,000 项。
- `signals`：最多 100,000 项；保存文件/表/列身份、表名、原始名、显示名、颜色、线宽、线型及偏移。原始列的 processingId/outputId 为空；派生列 file=-1，processingId/outputId 必须对应实际处理配置的输出，禁止重复身份和孤立输出。
- `objects` / `processing`：各最多 256 项。对象只允许 general/aircraft，rules 必须为空；处理配置只允许 processing，保存来源字段映射和有序规则。不保存计算数组，恢复后重算。
- `layout`：1–8 行/列、活动与最大化子图；`plots` 数量须与布局一致，各自保存时间图信号、Y 范围、归一化和线宽。
- `xRange` / `cursor`：共享 X 范围、游标模式和两个绝对时间（允许位于视窗外）。
- 每个 plot 的 `states` 必须保存 `enabled` 和 `lanes`。每行仅包含 source/name/visible，最多 64 行，所有来源引用有效。枚举存于每个 signals 条目的必填 enums 数组，每项为 value/text/color，最多 256 个唯一安全整数；空数组表示未定义枚举。位提取属于处理规则。状态配置随会话和模板保存，模板应用时统一重映射；隐藏行也保留来源引用。状态和航迹各自保留配置，同时启用时航迹优先显示。

数值必须有限，索引有效，来源身份和输出归属一致。对象字段/规则/输出 ID 唯一；规则只能引用本配置字段及此前输出。飞机对象要求当前启用坐标/姿态的预置字段，未启用字段可保留。关联航迹只能引用飞机对象。

每条规则显式保存算子参数，包括 samplePeriod、lossMode、counterStep、expectedStride、counterModulus、counterRate、counterTolerance、rateMode、rateWindowSeconds，不用缺字段推断历史默认值。丢包规则只有一个输出，rateMode=count/cumulative/window；双输出定义直接拒绝。切换指标保留该输出的身份与样式。规则语义见[对象与派生数据](objects-and-derived-data.md)。

主题、窗口大小、搜索、树展开和航迹配置面板折叠不写入会话。文件上限 16 MiB，QSaveFile 原子保存；格式验证失败不覆盖已有文件。恢复先暂存并验证全部来源，失败保留当前会话。缺失文件可重定位，重定位恢复后保持未保存状态。

## 航迹配置

每个 trajectory 保存 enabled、signals（独立于时间图的候选来源）、tracks（1–64 条）、active 和公共相机。每条 track 保存 id/name、objectId、visible/geographic、颜色、线宽（1–12）、三轴 axes 和 attitude。axes 为 -1 或有效且不重复的信号索引，位置/姿态来源须属于公共 signals；有坐标来源的航迹不可混合参考系。axes/geographic 仅保存在每条 track 中；顶层仅保存 active 索引，不保存重复镜像，出现旧镜像字段会拒绝读取。

attitude 包含 mode（0 关闭、1 欧拉、2 四元数）、四个 sources、radians/scalarLast/navigationToBody 和 order（0 Rz·Ry·Rx、1 Rx·Ry·Rz）；欧拉使用前三来源。缺失测量来源可保存，但运行时隐藏模型并给出原因。

camera 保存方位角、俯仰角、缩放与屏幕平移；rotation 为空时使用方位/俯仰，否则为单位四元数 [w,x,y,z]，模长平方误差小于 1e-5。viewScale 范围 [0,1e6]，0 表示自动适应；panDepth 范围 [-1e6,1e6]。地理原点、空间 LOD 和姿态几何不保存，由原始数据重建。

## 视图模板：换数据复用配置

在“会话”菜单选择“保存为视图模板…”保存 `.diview`；加载新数据后选择“应用视图模板…”。模板复用布局、活动/最大化子图、信号显示名称/样式、绑定、归一化及航迹/姿态/相机配置，不加载模板来源文件、不复制原始数据、不替换当前会话路径。当前数据的时间偏移保留。

应用前显示匹配预览：优先按表名和原始信号名匹配；没有同表名候选时按表序号和原始名匹配（例如不同文件名的单表 CSV）。仅唯一候选自动匹配，缺失/重名需手动指定。同一目标信号不可分配给多个模板来源。只有配置引用的信号需要匹配，未引用的已加载信号保留。

默认 X/Y 范围适应新数据；勾选“使用模板中的固定坐标范围”沿用模板范围。保留当前游标模式并重新定位，不复用模板绝对游标时间。全部映射和结构校验通过后才应用；取消或验证失败保留当前视图。应用后“会话 → 撤销上次模板应用”可一次恢复此前配置；来源、样式或偏移变化导致 Store 代际改变后不再允许旧模板撤销。模板应用会标记会话未保存，保存模板本身不清除会话修改状态。

模板独立格式 v5：顶层仅包含 `format = "DataInspectorViewTemplate"`、`version = 5`、`signals`、`view`。`signals` 只保存被视图引用的来源编号、文件名提示、表/原始信号名称、显示样式及枚举定义；`view` 仅保存布局、子图和 X 范围。不保存来源完整路径、偏移、对象定义或绝对游标位置，航迹对象关联转换为直接来源绑定。仅接受 v5，旧 v1/v2/v3/v4 模板会拒绝读取。结构和全部引用严格校验，失败不改变当前视图；文件上限 16 MiB，使用 QSaveFile 原子保存。

会话 v22 的处理规则必填 autoWordBits（布尔）及 byteOrder（little/big）。每个输出除了 id/name/series，还必填 startBit/bitCount/signedField/expression/inputs。inputs 为合并输出的独立输入组，其他操作保存空数组。位提取和公式允许 1–32 输出，bitCount=0 或 expression 为空时继承规则的公共标量缺省值；界面输出列表保存显式参数。有效字宽是来源派生缓存，恢复后重新计算；模板仍为 v5，不包含处理规则定义。
