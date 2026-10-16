# 仪器服务与 SCPI 引擎

`vna_service` 使用 `vna_core`、`vna_calibration`、`vna_measurement` 与 `vna_display` 的公开接口；`vna_scpi` 将已登记的命令转换为服务操作，不访问驱动。支持独立通道，SCPI 与 Web 共用服务及独占控制权，兼容范围以实际登记的命令子集为准。

按职责组织类型：instrument_settings.h 放置 InstrumentSettings、ConfigPatch 与配置关联的 TriggerSource/SweepMode；service_task.h 将内部 ServiceTask 与 Kind/Stage 放在一起。Measurement、OperationResult、InstrumentError、DataResult、InstrumentSnapshot 各自有独立的测量定义、异步结果、错误队列、查询数据和状态快照职责，保留独立头文件。instrument_service.h 声明服务入口并包含所需类型。Measurement::Parameter 的行为在 VnaMeasurement/src/measurement.cpp，InstrumentService 直接管理控制归属、设置、错误寄存器和任务 FIFO。ServiceTask 按值存储，Kind 区分物理配置 CONFIGURE 与测量目录 MEASUREMENTS，Stage 标明配置、触发和停止屏障；每个状态推进函数独立可读。测试受控设备和 Fixture 各自独立，通过 Core 的公开所有权注入构造函数接入；长实现位于对应 cpp，由 vna_service_test_support 链接到测试目标。

## 生命周期和线程

进程拥有 `AcquisitionCore`。初始化成功后构造 `InstrumentService(core, profile, simulator)`，在监听前调用 `BootstrapProfile(error)`；这是唯一允许阻塞等待的服务方法。它要求核心 READY、没有控制者且仪器配置尚未就绪，不限制设备身份前缀。`simulator` 在构造时冻结，缺省为 `true` 以兼容模拟测试；真实驱动必须传 `false`。服务必须先于核心销毁。

`IsSimulator()` 返回已冻结的驱动类别。配置就绪后，模拟服务按配置端口数返回 `VnaPro,NativeSimulator,<n>PORT,1.0`；真实服务原样返回核心缓存的设备身份，不伪装为模拟器。配置未就绪时统一返回 `VnaPro,UnverifiedBackend,0,1.0`。

Bootstrap 只准备默认目录与物理计划并保持 HOLD。服务器先恢复 Ch1 校准池及选择、保持校正关闭，再按 `instrument.sweep.defaultSweepMode` 应用启动缺省：默认 CONT 自动连续扫描，也可显式 HOLD；监听前排空该启动操作并释放内部控制权。扫描设置与测量目录仍使用启动默认，启动缺省不等同于厂商 Preset。浏览器连接与服务单元夹具均不触发此启动流程。

运行期间，服务、引擎和状态寄存器都在同一个 executor 调用。`ScpiEngine(service)` 自动尝试取得独占控制权，`HasControl()` 表示是否成功。`SubmitLine()` 接受不含 LF 的完整程序消息；返回 true 表示整条消息已受理，不能据此认为设备操作已经完成。

网络每约 5 ms 调用 `SetTransportOutputPending(...)` 和 `Poll()`，通过 `TakeOutput()` 取得完整响应消息，再处理 socket 的部分发送。多个查询响应单元用分号分隔，消息以 LF 结束；写命令没有 ACK。数组每轮最多编码 256 个值，每次最多分派 32 个单元，采集 future 从不在网络循环中阻塞等待。

断开调用 `Disconnect()` 后继续 `Poll()`。已经受理的完整程序消息、扫描和屏障继续推进，响应丢弃；`CanReleaseControl()` 为 true 后才能销毁引擎并接纳新控制连接。未成为完整消息的 socket 输入由网络层丢弃。持续扫描本身不阻止连接收尾，也不因断开而自动停止。核心关闭和 join 由进程拥有方在网络循环之外执行。

仪器错误 FIFO 和状态寄存器由服务持有，跨引擎/连接保留。Web 写入口使用服务签发的会话 token 和相同写授权；释放时排空已接受有限操作，再交还控制权。HTTP 会话合同见[服务设计](../docs/网分服务与远程控制设计.md)。

## 当前模拟配置

- InstrumentProfile 在启动时验证并冻结；默认 4 端口、频率 10 MHz–8 GHz，201 点，IFBW 1 kHz。全新服务只创建并选择 `Trc1/S21`；单端口配置回退为 `Trc1/S11`，均为未校准测量，初始 HOLD 且没有已完成帧。
- 仅模拟合同规定一个 frequencyRaw 计数等于 1 Hz，IFBW raw 采用相同整数；9N 表全零且为 opaque 数据，不代表真实功率、增益或校准表。
- InstrumentProfile 的 `defaultPowerDbm` 为启动时冻结的 double 功率，单位 dBm、范围 `[-120,100]`；每个扫描计划显式生成 N 个相同值，修改点数时重新填充，负数及小数不会被截断。
- 默认支持频率范围 10 MHz–8 GHz，由 instrument.frequency 配置，端点按整数 Hz 回读；端点交叉保留旧 span，联动另一端点，联动后超范围则拒绝整个候选。
- 点数 2–10001；IFBW 有效值为 10、100、1000、10000、100000 Hz，向上取适用值。频率、点数及 IFBW 等物理配置允许 Ready/Running，复用核心完整帧边界替换计划；不 Stop/Start，不改变 CONT/MAN、触发源或 runId。有限 SING/Trigger 前的请求先完成原测量，再按 FIFO 执行配置；执行时重新校验控制权、状态及逻辑 expectedRevision。
- 协议测量选择走 DISPLAY FIFO，不覆盖正在提交的配置候选；有限采集期间可受理并在先前完整帧终结后执行。保留的原生同步选择入口仅在 SCPI 持权且 FIFO 为空时使用；触发范围修改仍须持权且无待处理配置。
- `UpdateMeasurement(measurement, expectedRevision, owner)` 原位替换已存在的同名定义，保留名称、目录顺序、数量和当前选择；目录达到 128 项预算时也能更新。它与新增、删除使用独立的 MEASUREMENTS 任务，Ready 和 Running 都可受理，不要求 HOLD；执行时重新校验控制权、逻辑版本及能力，不存在的名称不会被隐式创建。
- 接收器映射仅采用模拟器明确的 IQ 布局，每点四端口、每端口 A.real/A.imag/B.real/B.imag。非比值参数接受已配置能力内的 `a1..a4` 或 `b1..b4` 和源端口 1..portCount（最高 4）；接收器字母大小写不敏感，测量名称保留大小写。

浏览器首次打开或刷新只读取已有服务目录，不调用启动初始化，也不把用户已设置的测量恢复成默认值。Meas 通过 `PUT /api/v1/measurements/{name}` 更新活动迹线，名称取自路径；新增、显示和删除归 Trace Config，Add Trace 通过后端 Clone 事务复制活动测量定义、分配未使用的 `Trc<n>` 并选中。这不增加任何 SCPI 命令。

## 测量目录与采集版本

`InstrumentSettings.revision` 是物理配置及测量目录的逻辑 CAS 版本，每个成功事务增加一次。`acquisitionRevision` 是核心最后成功下发的计划版本，对应 `CoreView.configRevision` 和 `MeasurementFrame.configRevision`。两者从启动时的 1 开始，但目录更新后可以不同；逻辑版本耗尽时在驱动调用前拒绝，不能回绕。

服务保存最后成功下发的 `activePlan_`，而不是仅从当前目录推断实际硬件状态。候选的频轴、IFBW、原始功率和表数据相同且源集合、接收器掩码被该计划覆盖时，只提交目录和逻辑版本，直接用同一完整原始帧重新计算。删除或减少需求保留实际计划的已有源和接收器；空目录也不下发虚构的新需求。刺激轴查询直接读取该成功计划的 Hz 轴，候选下发期间及准备失败后保留上次提交的轴，不重新构造另一份计划。

缺少激励源或接收器时，完整候选交给 Core FIFO，等待正在进行的完整多源帧结束再 Configure。设备成功后才提交目录、新逻辑版本、acquisitionRevision 和 activePlan_；旧帧保持不可变，直到当前物理版本的新完整帧到来前不能作为当前数据。CONT 保持连续运行，MAN 保持 armed 并等待用户触发；不调用 Stop/Start，不改变 runId，不重置 frameSeq。

SING 或手动 Trigger 的有限任务尚未完成时，目录写请求仍可受理，按服务 FIFO 在先前有限操作完整结束后执行；不提前更改正在采集的定义，不要求调用者先切 HOLD。前端已受理操作期间暂时禁用重复写，操作完成后重新开放。

准备缓冲失败未触碰设备，保留原目录、两个版本、计划及 CONT/MAN 状态。设备部分下发失败进入 Faulted，服务归一为 HOLD，不能把旧计划标为有效；Closed 同样归一 HOLD。执行时也核对核心物理版本与 acquisitionRevision，禁止绕过服务的 Core 配置更改破坏覆盖缓存。

状态 API 的 `revision` 用于写请求 CAS，`acquisitionRevision` 用于原始帧身份校验。帧 API 的 `revision` 保持物理版本，`measurementRevision` 标识此次计算所用目录的逻辑版本；浏览器同时配对这两个版本，避免并行读取时将旧参数曲线套用新标题。格式、刻度、标记和显示集合由 VnaDisplay 统一管理，进入服务 DISPLAY FIFO，独立显示版本不修改采集版本。

职责划分依据 [ZNB User Manual v74 第 77～79 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=77) 的迹线设置、硬件通道设置及改测量后的准备过程，以及[第 87～88 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=87) 的逐迹线数据流。本项目的双版本与完整帧边界是自身实现合同，不推定为厂商内部机制。

以上范围与换算是软件测试 profile，不能推定为 Keysight 或真实板卡的能力。

## 刺激缩放与显示手势

`ApplyStimulusZoom(change, expectedRevision, expectedDisplayRevision, owner)` 是同时涉及采集和显示的事务，复用控制权、授权代次、有限 FIFO 与异步完成机制。`change.action=APPLY` 指定活动可见直角坐标迹线、startHz/stopHz 与 min/max；矩形必须在当前频段和纵轴范围内，频率端点按整数 Hz 提交，点数、IFBW、触发源、运行模式及 runId 保持。圆图、CW、校准采集和无效物理计划明确拒绝。`OUT` 从服务历史恢复上一次频段及对应目标迹线刻度，不恢复目录、Marker 或活动选择。

服务先构造并校验配置、显示和历史候选；频段变化时等待 Core 在完整帧边界成功 Configure，随后一次发布逻辑配置、采集版本、显示版本及历史。准备或配置失败不提交候选；设备部分写入失败仍依核心合同进入 Fault，不能承诺硬件回滚。成功下发之后首帧故障不撤回已生效配置，应通过真实 Fault 状态报告。仅纵轴变化时不调用 Configure，逻辑与采集版本、已有完整帧及 Manual 布防状态保持，只提交显示和历史；完全相同的范围幂等成功，不增加版本或历史。

`StimulusZoom()` 返回 available、canZoomOut、reason、depth，HTTP 以 `status.stimulusZoom` 发布。Zoom In 与 Zoom Out 的可用性独立；切换活动迹线不妨碍恢复历史中的原目标。历史最多 16 项，这是项目资源与恢复规则；恢复前校验当前频段、原目标测量身份、格式及刻度仍匹配缩放后状态，防止覆盖随后外部修改。Marker 或活动选择变化不单独使历史失效。HTTP 请求与字段见[服务设计](../docs/网分服务与远程控制设计.md#6-web-接口与仪器面板)，不新增自创 SCPI 缩放命令。

原图内纵向平移复用显示域 `SCALE/referenceLevel`：只改变 Ref Value，保持每格跨度及 Ref Pos；右侧三角拖动则只改 Ref Pos。两者均不更改样本或采集计划。前端负责像素映射及短暂预览，正式刻度仍由服务显示 FIFO、owner 和 displayRevision CAS 校验。拖至垃圾箱也复用已有动作：命名迹线删除使用测量事务，图窗删除使用双 CAS 原子事务，单 Marker 及全部 Marker 使用显示关闭动作；不把解除显示关联的 `TRAC:DEL` 当成删除测量。最后迹线/图窗继续受保护，关闭 Marker 保留坐标和属性。

工具栏 Graphical Zoom / Zoom 1:1 属于前端临时视口：框选只改变该浏览器该图窗的 X/Y 显示范围，连续帧沿用视口，Reset 恢复原图，不写配置或 displayScale，也不调用本节刺激缩放事务。只读观察者可用；外部配置、格式、刻度或布局变化清除视口。图形缩放内平移只移动本地视口，原图平移才提交 Ref Value。进入 Stimulus Zoom 前先 Reset，避免把显示缩放当成真实扫频变更；当前没有服务端或 SCPI Graphical Zoom 合同。

## 已实现范围

支持共享显示域的标准格式、尺度、M1～M10 与有界只读观察连接；以及设计白名单中的身份、错误、ESE/ESR/SRE/STB、OPC/WAI、线性频率/点数/IFBW、触发源/范围、CONT/SING/HOLD、INIT:CONT、空闲 MAN 下 INIT、测量定义/选择/目录/删除、ASCii,0、SENS:X? 和 CALC:DATA? RDATA。支持正式短名/全名、实际通道编号后缀、分号消息、相对路径、单/双引号、指数数值与 Hz/kHz/MHz/GHz。测量名称保留大小写。

`CALC:PAR:SEL` 选择测量，不隐式显示隐藏项。`DISP:WIND<w>[:STAT]`、`SIZE`、`CAT?`、`TRAC<t>:FEED/DEL/SEL/STAT` 与 Y Scale 操作映射到共享图窗域；w/t 预算均为 1～128，局部槽身份支持稀疏编号并独立于 Trc 名称，删除槽不压缩其他槽号。CAT 返回局部槽号，FEED/DEL 只写且不删除测量定义；窗口 OFF 保护最后一个图窗。只读观察连接可查询所有已登记窗口状态、目录、尺度。CALC:FORM 增加官方 SWR、UPHase、GDELay、POLar；Web 特有格式没有自创 SCPI 名称。命令表及官方依据见[服务设计](../docs/网分服务与远程控制设计.md#42-首期实现命令)。

SING 的内部路径是手动 Start、一次完整 Trigger、Stop 屏障；OPC 响应前通道进入 HOLD。MAN+CONT 等待 INIT；`INIT:CONT OFF` 选择 Single 并经过 Stop 屏障进入 HOLD，即使此前已选 Single 也会停止。`*RST` 逐通道恢复默认测量、刺激及 HOLD/IMMEDIATE，保留校准集，并恢复默认全局显示和一个图窗。支持 ASCii,0 和 REAL,64（NORM/SWAP）；忙时 INIT、中止 ABOR、外触发及组数尚未开放；机械校准范围见下节。

Web 扫描控制另外保存 `selectedSweepMode`（CONT/SING，默认 CONT），与报告当前运行状态的 `sweepMode`（CONT/SING/HOLD）分开。`/api/v1/sweep-control` 改选 CONT 时通过服务 FIFO 安全切换到连续运行，保留当前触发源；IMM 自动扫描，MAN 直接布防。改选 SING 时经过当前完整帧 Stop 屏障后保持 HOLD，等待显式 start。source 变化保留队首执行时的运行或停止意图：正在运行或布防时，经过完整帧 Stop 屏障后以新源重启；已停止时只提交源选择。运行中的 IMM/Continuous 切到 MAN 后直接等待 Trigger，无需另一次 start；MAN/Single 切到 IMM 后完成一帧并 HOLD。重复选择当前值不停止或重启。start 保留扫描模式与触发源，供 Sweep Control 和工具栏 Restart Sweep 共用；stop 保留选择，兼容 single 动作在同一事务中选 Single 并启动。IMM/Single 完成后 HOLD，MAN/Single 等显式 Trigger 完成一帧后 HOLD，MAN/Continuous 每次 Trigger 后继续布防。Trigger 不会自动启动已停止的周期。停止和有限任务终结均保留所选模式。新动作按控制者及授权代次串行执行，不携带物理参数草稿的 expectedRevision；异步完成与状态读回沿用原 HTTP 操作记录。原机静态菜单、命令依据及尚未确认的 GUI 状态差异见[核对记录](../docs/research/ZNB8点数与中频带宽菜单核对.md#sweep-control-显示与当前控制语义)。

旧 `/api/v1/operations` 的 single/continuous 继续通过 RunSingle/RunContinuous 强制 IMM，SCPI 既有命令保持原合同；Web 新入口不调用这两个兼容快捷动作。尚未支持多次扫描、Restart Manager、批量通道运行控制、Sweep Controller 的 Sweep Info 窗口、External、Multiple、Trigger Manager、Point 逐点触发、Trigger Delay 或 Pipelining，界面须灰显并解释原因。各通道可分别运行；多通道场景仅支持当前通道触发范围，SCPI 请求 ALL 明确拒绝，不伪装为已经全部触发。

### 通道上下文

最多同时 16 个通道，迹线名称及 128 条迹线预算由仪器统一管理。每条测量定义带 `channelId`；图窗可混合显示多个通道，通道不由图窗决定。选择迹线时同步选择其所属通道，也可显式选择无迹线的通道；`ActiveChannelId` 返回当前选择。旧 `Snapshot/Settings` 继续提供活动通道别名，`Snapshot.channels` 提供各通道的设置、CoreView、校准、刺激缩放历史与在途状态。

配置、扫描、触发及校准 API 的末尾 `channelId=0` 表示受理时解析当前上下文，具名迹线按所有者解析。任务在入队时冻结目标，随后选择另一通道不会改写归属；显式通道与具名迹线不匹配时拒绝。控制权、授权代次和服务 FIFO 仍为整台仪器共用，不对每通道额外开放控制者。

Core 保留单个物理设备和 worker，在完整帧边界为可运行通道轮转；切换物理计划不增加其他通道的逻辑采集版本。各通道持有独立 acquisitionRevision、runId、frameSeq 和不可变完整帧，Manual 通道没有触发时不采集，也不阻塞其他 Free Run 通道。设备故障会停止共享设备上的所有采集，并在恢复时重新验证计划。互不影响指配置和数据身份独立，不代表硬件同时扫描。

`CreateChannel` 的 new 使用 profile 默认频段、IFBW、功率和默认迹线；copy 复制源通道全部设置、已保存校准和迹线/Marker，校准可变选择独立，完整帧重新实际获取。`newDiagram` 决定放在新图还是活动图。DeleteChannel 删除所属迹线及显示，保护最后通道；删除屏障完成后才移除上下文。预算及复制采集中通道的拒绝是项目合同。

`InstrumentSettings.powerDbm` 为当前通道的基准源功率，`ConfigPatch.hasPowerDbm` 控制补丁字段。仅接受有限的 −120…100 dBm，保留负数与小数；MakePlan 将该值填入逐点 `SweepPlan.powerDbm`，不再固定使用 profile 默认。修改频段、点数或功率只改变目标通道；校准兼容性同时校验真实频率轴、IFBW 和逐点功率。Zoom 历史按通道保存，Marker 轴重定位/采集版本绑定只处理所属迹线。

刺激轴从已提交物理配置生成，不要求先扫描。RDATA 必须来自当前有效 acquisitionRevision 和 run 的完整帧；纯目录更新可即时重新计算已有 IQ，物理计划更新后的旧帧、有限扫描未终结、故障或非有限数据都报告错误，不返回伪造全零曲线。连续扫描下多条独立查询可能跨帧；同轮读取需要单次完成/HOLD 后保持配置和触发不变。

`MeasurementData(selection, calibrated, channelId)` 按目标通道测量定义顺序解析从 1 开始的序号，一次冻结同一完整帧和校正系数，逐迹线排列每条曲线的 N 对 Re/Im。序号、帧版本、所有迹线及全部采样点校验通过后才返回数组，失败不交付部分结果，也不修改当前选择。此批量入口在 `calibrated=true` 时严格要求已启用、刺激兼容且覆盖每个请求 S 参数的校准；缺失、关闭或仅覆盖部分端口均拒绝，不回退原始数据。现有单迹线入口的未校准行为保持兼容；当前一/二端口模型不能提供完整四端口校准矩阵。

`DeleteAllMeasurements(owner, channelId)` 在一个服务 FIFO 事务中清理目标通道的测量、显示引用及选择，其他通道保持。`ResetMeasurements(owner, channelId)` 先准备默认配置和显示，再等待当前完整帧停止、真实下发默认物理计划，最后提交默认 S21（单端口 S11）、默认刺激/功率及 HOLD/IMMEDIATE。默认名称优先 Trc1，其他通道占用时使用首个空闲 Trc 编号；校准集存储保留。候选准备失败不改变运行；设备下发失败报告错误并保持停止状态，不回报复位成功。

软件预算：单消息 16 KiB/128 单元，待分派消息 32，服务操作队列 32，pending OPC watches 32，仪器错误 FIFO 32，输出 8 MiB。超预算明确拒绝或记录正式错误；四口校准及批量读取扩展见[校准使用说明](../docs/校准接入与使用.md)。

## 验证

顶层 CMake 开启 BUILD_TESTING 时，`vna_scpi_tests` / `service_scpi` 同时验证服务和协议。实际模拟 DLL 用于单位、接收器数据、消息边界及 10001 点输出；可控设备替身用同步门验证阻塞扫描、OPC 上限、断线收尾以及失败单次保留旧帧但拒绝作为当前数据。

仪器能力类型和默认值在 `instrument_profile.h`，验证行为在 `instrument_profile.cpp`。YAML 由 Server 使用 yaml-cpp 解析，再传值给服务；服务不依赖配置文件或 YAML 类型。HTTP 返回能力只读视图，SCPI MIN/MAX 查询与设置从同一 Profile 取边界。2 端口配置禁止 RAW 第 3/4 端口的源和接收器，4 端口配置允许全部 RAW 波量；两者都支持 2×2 未校准 S 参数。

`instrument_profile` 验证 1/2/4 端口的新服务默认测量、HOLD 和无初始帧，并独立检查默认 S 参数确实经过复比值计算。`service_measurement` 覆盖原位切换、源 2 参考波、旧帧失效、能力/控制权/状态/版本拒绝、设备准备失败回退、满目录更新及排队版本竞争；历史 RAW 和黄金向量用例显式准备自己的测量目录。

`live_measurement` 使用阻塞关卡核对 CONT 中同帧重算、新增源的完整帧边界、旧帧计划与 IQ 深不可变、准备失败仍运行、目录新增/删除不重复下发、CAS/控制者/2 端口能力及 MAN armed 无隐式触发。`live_configuration` 独立验证 CONT 中点数与多参数原子修改、完整帧屏障、准备失败保留旧运行、MAN 布防、有限采集排队及 CAS/控制权拒绝。

`service_scpi_windows` 使用实际模拟驱动核对两个图窗的局部第一槽、稀疏槽、删除后稳定身份、最大化/恢复、只读目录和 Web 控制者下的 SCPI 观察；`service_scpi_display` 核对新增标准格式、Polar/Smith 复数标记读数及不支持格式的明确错误。

共享仪器显示状态、完整帧投影、跨协议同步和显示写入活动图见[共享显示域设计](../docs/共享显示域设计.md)。

显示域接口采用独立 displayRevision CAS；控制者与观察者读取同一 DisplayState。观察连接不自动升级，写命令、事件读清及错误出队整条拒绝并关闭；不会释放其他控制者，也不会修改全局错误队列。

## 机械校准与系数集

`ApplyCalibration(change, expectedCalibrationRevision, owner)` 复用已有控制者、有限 FIFO、操作 future 和 OPC 屏障。校准状态由 `CalibrationWorkspace` 唯一管理，`calibration.h` 是校准请求、步骤和系数集元数据的公开合同；数学求解在无设备依赖的 `VnaCalibration`，完整帧归一化与可选校正在 `VnaMeasurement`。`InstrumentSnapshot` 携带同一次读出的原始帧、校准状态和不可变 `correction`，协议不能在每条迹线重新取得另一当前帧。

首版支持能力范围内任意单端口的 OSL、任意不同端口对的 SOLT。校准件明确采用理想 50 Ω模型：OPEN Γ=+1、SHORT Γ=−1、LOAD Γ=0、零损耗零延时匹配 THRU。双端口可附加 ISOLATION 两方向实测，不选时显式假设隔离为零；系数集保存 `includeIsolation`，选择另一个集时状态按该集回报此假设。尚不支持真实校准件参数模型、CW/重复整数 Hz 频点、TRL、ECal 硬件控制、频轴插值或厂商校准文件直接兼容。

BEGIN 保存一次原 TriggerSource/SweepMode 后切到 MANUAL/HOLD；CAPTURE 的单次标准采集仍结束于 HOLD。正常 FINISH/CANCEL 复用已有模式状态机恢复原 CONT、MAN/SING 或 HOLD，future/OPC 等核心实际恢复后终结，不只修改设置字段。数学失败继续保留会话与旧集；恢复启动失败报告失败并保留已生成的有效新集，故障取消只清理会话/HOLD而不重启。session 存在期间，刺激、测量目录、RUN、INIT 的队首执行检查均拒绝，显示格式、Marker 等独立显示事务仍可使用。拒绝不仅在网络入口检查，已排在 BEGIN 后的旧配置请求也不能绕过冻结的校准刺激。

CAPTURE 先完成旧运行的 Stop，再以新 run ID 启动 MANUAL、触发完整多源帧、经过最后 Stop 屏障，核对成功触发票据的 run/configRevision/frameSeq 后才保存标准。标准始终经过原始 `b_i/a_j` 计算，即使已有校正集启用也不校正标准。失败不修改先前成功的标准，不将保留的旧 DUT 帧标为本次标准。FINITE 操作进行期间，当前数据读取仍遵循已有屏障限制。

`CalibrationPreview(sessionId, stepId)` 只读已确认标准的原始复数向量，供校准向导绘制 dBMag/Phase；反射标准返回该端口 Spp，THRU/ISOLATION 返回所选端口对的正向传输。频轴和 IFBW 来自 session 冻结刺激，run/frameSeq 来自该标准实际捕获身份。未采集、未知步骤和旧 session 返回 unavailable 及请求身份，不构造零曲线、不套用旧校准、不改变 DUT 目录或保留整个采集帧；核心故障后仍可查看先前成功标准的历史数据。

实际扫描源与接收机是测量目录、活跃 session、选中校准集所需端口的并集；关闭校正或缩减目录不强制缩小已下发计划。十二项校正从同帧四个 S 参数求完整端口对，只有一条 S21 类迹线时仍保留反向源。纯采集需求扩张更新 `acquisitionRevision`，不改变 `settings.revision` 或物理校准指纹，也不创建隐藏测量定义。

校准集有效性比较设备身份、精确 Hz 轴、IFBW、逐点 double dBm 功率；名称、图窗、格式和采集版本不参与物理指纹。功率必须有限且处于 −120～100 dBm，直接服务导入同样校验，不依赖协议入口；负数与小数变化也会使旧集不适用。集摘要中的 startHz、stopHz、points、ifBandwidthHz 始终取自该集冻结的刺激，方便管理器解释当前不兼容原因，不以当前仪器设置覆盖。刺激变化后保留集并显示 incompatible，`correction` 为空；恢复完全相同刺激可重新兼容。Faulted、Closed 或绕过服务的计划版本变化也不能提供当前校正上下文。`MeasurementData(true)` 默认读取可用校正结果，`MeasurementData(false)` 始终读取未校正归一化值；非比值 a/b 波量不应用误差校正。

新 session 不替换已完成集；只有全部标准可用且每个频点求解成功，FINISH 才以不可变数据一次发布、选中并启用新集。支持 ENABLE/SELECT/DELETE_SET/IMPORT_SET 与只读 `FindCalibrationSet(id)` 导出，容量及服务端恢复合同见下文。导入在受理入队时冻结名称、刺激和系数，队首再次检查端口能力、有限系数、轴、IFBW、功率、形状和容量；调用者保留的可变别名不能改变已受理内容。用户导出/导入使用一/二口 schema 2、四口 schema 3，功率以 `powerDbm` 数字数组保存，ID 仍是十进制字符串。旧 schema 1 只兼容每项精确字符串 `"0"` 的原功率轴；任意非零旧编码拒绝并要求重新校准。

校准有独立 revision；非 SCPI 请求严格使用 CAS，SCPI revision=0 保持既有省略版本惯例。取消复用 FIFO：已接受扫描先停止，再清除相同 session；同 session 的 CANCEL 允许其 expected revision 落后于先前 capture 提交，以免扫描过程中发出的取消因正常完成而变旧。session ID 仍必须准确，旧 session 请求不能取消新 session。采集故障后仍允许取消 session，故障不因此恢复。

采集/求解的工作区错误仅由已通过版本、session、标准和刺激检查的任务回写。旧 session、失配 CAS 或不存在标准的拒绝只返回操作错误，不能污染当前 session.error 或改变其校准版本。

现有 VN08/VN10 模拟驱动生成固定 DUT，未提供实际换接 OPEN/SHORT/LOAD 的模型。点击向导不会凭空替换其原始测量；不换件得到重复标准时求解拒绝奇异数据。功能验收使用按源区分的受控 `DeviceSession`，独立前向误差模型生成标准和 DUT，经过真实 core/service 采集链；它不改变产品模拟器原生 ABI。

`service_calibration` 覆盖新扫描身份、任意两端口 SOLT、实测隔离、仅一条迹线时的反向源需求、原始读取与校正开关、已有校正时标准采集绕过校正、排队配置/RUN拒绝、取消屏障、奇异求解保留旧集、非有限导入拒绝、刺激恢复兼容及硬件故障拒绝旧帧。`service_calibration_storage` 验证 256 集容量、逐通道与全局字节预算、恢复原子性及失败保旧池。

完整四口使用 `FOUR_PORT`（固定 P1～P4）：每口 O/S/M 加全部六对理想 THRU，共 18 步；开启逐对隔离时 24 步。每频点发布 receiver-major 的 16 格误差表，交由数学模块一次 4×4 联合校正。`service_four_port_calibration` 以独立前向模型生成标准与另一个稠密 DUT，核对全部误差项和 16S，并验证失败保旧集。四口导出/导入 schema 3，一/二口继续 schema 2。

每通道最多 256 集、1 GiB 保存数据，所有通道合计最多 1 GiB；当前轴 512 MiB 与旧轴 32 MiB 派生缓存独立计费，不随原池扩容。这些是系数及元数据的逻辑预算，不是进程 RSS 限制，解析、求解和恢复存在临时副本。`RestoreCalibration` 只供无控制者、无任务的启动阶段使用，保留 ID、名称、系数与选择并关闭校正。安装异步 `CalibrationSaveHandler` 后，完成、导入、删除、选择或激活必须等待回调成功再提交 workspace，失败保留原池及选择。HTTP operation 和 SCPI OPC 包含此屏障；仅启停校正与采集会话不落盘。当前服务器仅为 Ch1 保存和恢复，其他逻辑通道维持内存行为。存储格式及部署边界见[校准使用说明](../docs/校准接入与使用.md)。
