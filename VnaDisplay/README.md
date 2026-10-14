# 共享显示域

`VnaDisplay` 是仪器显示语义的唯一实现。它依赖 `VnaMeasurement` 和不可变采集帧，不依赖 `VnaService`、HTTP、SCPI 或任何 GUI。服务执行线程持有一个 `DisplayWorkspace`；该模块不创建线程、租约或采集状态机。

- `DisplayWorkspace` 保存活动测量、活动图窗、最多 128 条迹线的格式、尺度、圆图半径、可见性及每迹线 M1～M10，也保存全局 Info Table、信息显示选项、每图窗信息字段开关及每迹线字段位置。`Snapshot()` 返回独立副本；`State()` 只供服务执行线程即时读取。
- `DiagramCollection` 维护显式图窗、有序迹线归属及独立 `traceSlots`，格式不参与分组；扁平 `displayedNames` 只由图窗派生，不能单独作为归属状态写入。图窗槽允许稀疏编号，删除成员不会压缩其他槽；原生新增自动采用最小空闲槽。自动新增图窗分配 1～128 内最小空闲图号，显式稀疏图号不占满全部预算。每个图窗保存主迹线，决定轴刻度；可见迹线所需的矩形、Smith、Inv Smith、Polar 背景允许叠加，终端只从后端格式和半径派生像素几何。`ArrangeDiagrams()` 返回全部已保存图窗，最大化只改变显示方式。默认保留图窗 1，最后图窗不能删除。项目预算 128 个图窗，允许 Split All 每迹线一窗，不宣称这是 ZNB 的硬件上限。
- `DiagramLayout` 是值对象，空结构沿用自动网格；相对分割第一次从正式网格转换，然后精确二分目标。它隐藏分割树，向协议提供归一化区域。`DiagramCollection::Place()` 在同一候选内完成中心归属或上下左右分割；移动保留测量及标记，空源图窗与兄弟区域合并，重编号同步映射。自动网格最后的空槽在转换时交给相邻兄弟图窗；完整矩阵保持原行列位置。显式排列、整组矩阵、Overlay 和 Split All 清除自定义结构，最大化保留结构。
- `Reconcile()` 在测量目录提交时保留同名显示设置，移除失效项。删除独占迹线会删除其空图窗并连续重编号；主动创建的空显示窗口保留。候选工作区可在提交采集计划前校验，防止显示版本耗尽造成部分提交。测量量改变后的默认格式由仪器目录事务应用，不以格式兼容性伪造 Smith 反射限制。
- `ReplaceSParameterMatrix()` 在同一候选中创建完整端口矩阵及逐迹线默认显示：对角反射使用 Smith，非对角传输使用 dB Mag，圆图半径为 1，Marker 关闭。同名旧定义不继承设置；普通新增及单参数修改的默认格式保持独立，前端不能再逐条补写整组格式。
- `ReconcileAxis()` 在物理配置候选中按真实刺激轴协调保存的标记频率和离散点，保证频段改变后保存坐标与读数一致；同轴 IFBW 或源需求变更保留合法 CW 原始下标。核心成功提交后调用 `BindAcquisitionRevision()` 更新已验证下标的隐藏采集版本，该绑定不改变可见设置或显示版本。
- `Apply()` 先构造候选、校验并计算，全部成功才递增独立显示版本。无变化操作不增版本，失败不修改状态；到达 `uint64` 上限时拒绝改变状态。控制权、预期显示版本与 FIFO 由调用者的服务执行器统一管理。
- `TraceProjection` 用完整复数数据执行十三种常规格式转换及一次 Autoscale，区分有效值、无效点、负无穷、未定义相位和溢出。旧 `realimag` 名称继续支持 API，但不进入常规菜单。Polar、Smith、Inv Smith 均保留原复数实虚坐标；只有明确的反射 S 参数才返回归一化阻抗和导纳，未知测量类型、传输参数或原始波量不冒充负载解释。不假定实际参考阻抗；字宽、字体、DPR 和像素坐标由显示终端处理。
- `TraceSequenceFormatter` 在完整真实频轴上展开相位和计算 Delay / Delay Derivation；非均匀轴使用中心与边界二次插值差分，无效段断开，CW 和点数不足返回未定义，不伪造零延迟。群延迟单位为 s，角频率二阶导数单位为 s²；当前实现没有厂商 Aperture Points 回归窗口，不宣称相同噪声平滑行为。
- `MarkerResolver` 默认对真实频轴上的复数值进行线性插值；展开相位和频率导数则取同一完整序列投影，插值这些已计算的值，不能对单个复数求导。Polar 的默认读数采用幅值与相位（度），曲线与标记定位仍使用实部 / 虚部；`markerPoints` 为拖动预览提供相同的后端计算。零幅值保留有效幅值 0，独立 `secondaryState` 将相位标为未定义。显式原始采样下标和步进改为离散点；CW 同频点仍保留采样下标。传输适配器可以抽点绘图，但标记独立读数必须来自完整原始数组。
- `ScaleCalculator` 将范围和每格尺度、参考值、参考位置规范化成同一个范围，Autoscale 只在命令执行时拟合一次。Log Mag 保存正幅值 min/max，每格以 decade 表达，参考值为正幅值；绘图在 log10 轴上映射，读数仍是幅值，不转换成 dB。之后的采集帧只改变曲线，不更新已应用尺度。

`FrameContext` 使用一次读取的不可变帧、当前采集版本与运行编号，以及同轮读取的不可变校准系数。Autoscale 先通过统一测量处理器校正，和 HTTP/SCPI 曲线及 Marker 使用同一数值路径。它也可以在同步命令期间借用已提交真实刺激轴；工作区不会保存该借用。Autoscale 和采样移动需要当前完整帧；物理版本检查不要求 `frameSeq` 等于浏览器旧序号，避免连续扫描时正常操作频繁失败。首帧前仍可按已提交刺激轴保存合法 Marker 频率，超出扫描范围原子拒绝，不由 HTTP 或 SCPI 各自钳制。纯显示配置无需获得采集数据。启用标记默认取真实频段中点；若没有可验证的刺激轴，先保留未定位置，首次投影以实际频段中点计算。

窗口刷新和另一观察窗口读取同一服务内存状态。此模块没有磁盘恢复合同；关闭并重新启动服务会采用初始显示设置。

`DisplayState.infoTable` 保存全局容器显隐和 Bottom/Left/Right，默认隐藏、Bottom；`annotations` 保存 `activeTraceOnly=false`、`stimulusInfoVisible=true` 和百分比 `fontScale=100`，字号合法范围为 80～170。`DisplayDiagram.markerInfoVisible` 默认开启，控制该图窗所属迹线的信息字段，放入 Table 后仍遵循该开关；`TracePresentation.markerInfoLocation` 默认 Diagram，可独立迁入 Table；`markerInfoArea` 默认 TopRight，保存九个预设图内锚点，迁移时不丢失，Clone 复制源锚点。归属及可选锚点在同一候选提交；Table 携带锚点原子拒绝。显隐不改变 Marker 开关或坐标，隐藏 Table 不重写字段归属，字段迁移不自动显示容器。全局 Active Trace Only 依据 `selectedMeasurement`，不以每窗主迹线代替。

信息字段使用已有完整帧 Marker 投影；Table 与图内读数共享来源。字体字宽、标题列数、容器像素尺寸和拖动预览由终端处理，不能反向成为新的仪器权威状态；`stimulusInfoVisible` 只控制 Marker 频率，不影响通道行。全部动作沿用候选原子提交、无变化不增版本及版本耗尽拒绝，不触发采集配置。原机依据和项目边界见[Info Table 与标题排版核对](../docs/research/ZNB8信息表与标题排版核对.md)，操作验收见[Info Table 操作验收](../docs/InfoTable操作验收.md)。

单元验收运行 `display_workspace`、`diagram_collection`、`diagram_layout`、`trace_placement`、`trace_projection`、`trace_formats` 和 `marker_collection`，覆盖快照、版本与原子拒绝、图窗局部槽、混合格式不拆窗、显式增删及重编号、最大化保留归属、Split/Overlay、一次 Autoscale、刺激轴协调、完整频轴格式与标记、异常值及圆图物理解释。分割回归验证 1～128 个区域的边界、互不重叠及总面积，四方向目标二分、删除合并、无变化操作及容量拒绝，也验证移动不重置标记和矩阵重新排列。

`trace_formats` 补充十三种名称与 legacy 解析、SWR 奇点及大幅值、Log Mag 原幅值和尺度原子校验、原坐标圆图与反射物理解释、导纳短路状态、展开相位标记、非均匀频轴的解析群延迟和二阶导数、无效段 / CW / 10001 点完整序列。`Calculate()` 的测量元数据重载决定物理解释；未提供时只返回一般复数结果。`Autoscale()` 的序列格式必须提供真实频轴，不能用点序号替代 Hz。
