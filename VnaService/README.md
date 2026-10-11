# 仪器服务与 SCPI 引擎

`vna_service` 只使用 `vna_core` 公开接口；`vna_scpi` 将已登记的 Keysight N52xxB 命令转换为服务操作，不访问驱动。当前实现单通道，SCPI 与 Web 共用服务及独占控制权，兼容范围为已登记的 PNA 命令子集。

按职责组织类型：instrument_settings.h 放置 InstrumentSettings、ConfigPatch 与配置关联的 TriggerSource/SweepMode；service_task.h 将内部 ServiceTask 与 Kind/Stage 放在一起。Measurement、OperationResult、InstrumentError、DataResult、InstrumentSnapshot 各自有独立的测量定义、异步结果、错误队列、查询数据和状态快照职责，保留独立头文件。instrument_service.h 声明服务入口并包含所需类型。Measurement::Parameter 的行为在 VnaMeasurement/src/measurement.cpp，InstrumentService 直接管理控制归属、设置、错误寄存器和任务 FIFO。ServiceTask 按值存储，Kind 区分物理配置 CONFIGURE 与测量目录 MEASUREMENTS，Stage 标明配置、触发和停止屏障；每个状态推进函数独立可读。测试受控设备和 Fixture 各自独立，通过 Core 的公开所有权注入构造函数接入；长实现位于对应 cpp，由 vna_service_test_support 链接到测试目标。

## 生命周期和线程

进程拥有 `AcquisitionCore`。初始化成功后构造 `InstrumentService(core, profile)`，在监听前调用 `BootstrapMockProfile(error)`；这是唯一允许阻塞等待的服务方法。它检查缓存身份必须以 `VnaPro,NativeSimulator,` 开头。服务必须先于核心销毁。

运行期间，服务、引擎和状态寄存器都在同一个 executor 调用。`ScpiEngine(service)` 自动尝试取得独占控制权，`HasControl()` 表示是否成功。`SubmitLine()` 接受不含 LF 的完整程序消息；返回 true 表示整条消息已受理，不能据此认为设备操作已经完成。

网络每约 5 ms 调用 `SetTransportOutputPending(...)` 和 `Poll()`，通过 `TakeOutput()` 取得完整响应消息，再处理 socket 的部分发送。多个查询响应单元用分号分隔，消息以 LF 结束；写命令没有 ACK。数组每轮最多编码 256 个值，每次最多分派 32 个单元，采集 future 从不在网络循环中阻塞等待。

断开调用 `Disconnect()` 后继续 `Poll()`。已经受理的完整程序消息、扫描和屏障继续推进，响应丢弃；`CanReleaseControl()` 为 true 后才能销毁引擎并接纳新控制连接。未成为完整消息的 socket 输入由网络层丢弃。持续扫描本身不阻止连接收尾，也不因断开而自动停止。核心关闭和 join 由进程拥有方在网络循环之外执行。

仪器错误 FIFO 和状态寄存器由服务持有，跨引擎/连接保留。Web 写入口使用服务签发的会话 token 和相同写授权；释放时排空已接受有限操作，再交还控制权。HTTP 会话合同见[服务设计](../docs/网分服务与远程控制设计.md)。

## 当前模拟配置

- InstrumentProfile 在启动时验证并冻结；默认 4 端口、频率 10 MHz–8 GHz，201 点，IFBW 1 kHz。全新服务只创建并选择 `Trc1/S21`；单端口配置回退为 `Trc1/S11`，均为未校准测量，初始 HOLD 且没有已完成帧。
- 仅模拟合同规定一个 frequencyRaw 计数等于 1 Hz，IFBW raw 采用相同整数；9N 表全零且为 opaque 数据，不代表真实功率、增益或校准表。
- 默认支持频率范围 10 MHz–8 GHz，由 instrument.frequency 配置，端点按整数 Hz 回读；端点交叉保留旧 span，联动另一端点，联动后超范围则拒绝整个候选。
- 点数 2–10001；IFBW 有效值为 10、100、1000、10000、100000 Hz，向上取适用值。频率、点数及 IFBW 等物理扫描配置仍只允许 HOLD 且无未完成有限测量；执行时再次校验状态和逻辑 expectedRevision。
- 配置候选提交期间，服务的直接测量选择和触发范围修改报告状态冲突，防止整份候选提交覆盖已成功修改的字段；两者也必须持有控制权。有限测量期间仍允许选择已定义的曲线。
- `UpdateMeasurement(measurement, expectedRevision, owner)` 原位替换已存在的同名定义，保留名称、目录顺序、数量和当前选择；目录满 32 项时也能更新。它与新增、删除使用独立的 MEASUREMENTS 任务，Ready 和 Running 都可受理，不要求 HOLD；执行时重新校验控制权、逻辑版本及能力，不存在的名称不会被隐式创建。
- 接收器映射仅采用模拟器明确的 IQ 布局，每点四端口、每端口 A.real/A.imag/B.real/B.imag。非比值参数接受已配置能力内的 `a1..a4` 或 `b1..b4` 和源端口 1..portCount（最高 4）；接收器字母大小写不敏感，测量名称保留大小写。

浏览器首次打开或刷新只读取已有服务目录，不调用启动初始化，也不把用户已设置的测量恢复成默认值。Meas 通过 `PUT /api/v1/measurements/{name}` 更新活动迹线，名称取自路径；新增、显示和删除归 Trace Config，Add Trace 复制活动测量定义并生成未使用的 `Trc<n>`。这不增加任何 SCPI 命令。

## 测量目录与采集版本

`InstrumentSettings.revision` 是物理配置及测量目录的逻辑 CAS 版本，每个成功事务增加一次。`acquisitionRevision` 是核心最后成功下发的计划版本，对应 `CoreView.configRevision` 和 `MeasurementFrame.configRevision`。两者从启动时的 1 开始，但目录更新后可以不同；逻辑版本耗尽时在驱动调用前拒绝，不能回绕。

服务保存最后成功下发的 `activePlan_`，而不是仅从当前目录推断实际硬件状态。候选的频轴、IFBW、表数据相同且源集合、接收器掩码被该计划覆盖时，只提交目录和逻辑版本，直接用同一完整原始帧重新计算。删除或减少需求保留实际计划的已有源和接收器；空目录也不下发虚构的新需求。

缺少激励源或接收器时，完整候选交给 Core FIFO，等待正在进行的完整多源帧结束再 Configure。设备成功后才提交目录、新逻辑版本、acquisitionRevision 和 activePlan_；旧帧保持不可变，直到当前物理版本的新完整帧到来前不能作为当前数据。CONT 保持连续运行，MAN 保持 armed 并等待用户触发；不调用 Stop/Start，不改变 runId，不重置 frameSeq。

SING 或手动 Trigger 的有限任务尚未完成时，目录写请求仍可受理，按服务 FIFO 在先前有限操作完整结束后执行；不提前更改正在采集的定义，不要求调用者先切 HOLD。前端已受理操作期间暂时禁用重复写，操作完成后重新开放。

准备缓冲失败未触碰设备，保留原目录、两个版本、计划及 CONT/MAN 状态。设备部分下发失败进入 Faulted，服务归一为 HOLD，不能把旧计划标为有效；Closed 同样归一 HOLD。执行时也核对核心物理版本与 acquisitionRevision，禁止绕过服务的 Core 配置更改破坏覆盖缓存。

状态 API 的 `revision` 用于写请求 CAS，`acquisitionRevision` 用于原始帧身份校验。帧 API 的 `revision` 保持物理版本，`measurementRevision` 标识此次计算所用目录的逻辑版本；浏览器同时配对这两个版本，避免并行读取时将旧参数曲线套用新标题。格式、刻度、标记和显示集合仍是独立显示操作，不进入服务配置队列。

职责划分依据 [ZNB User Manual v74 第 77～79 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=77) 的迹线设置、硬件通道设置及改测量后的准备过程，以及[第 87～88 页](https://scdn.rohde-schwarz.com/ur/pws/dl_downloads/pdm/cl_manuals/user_manual/1173_9163_01/ZNB_UserManual_en_74.pdf#page=87) 的逐迹线数据流。本项目的双版本与完整帧边界是自身实现合同，不推定为厂商内部机制。

以上范围与换算是软件测试 profile，不能推定为 Keysight 或真实板卡的能力。

## 已实现范围

支持设计白名单中的身份、错误、ESE/ESR/SRE/STB、OPC/WAI、线性频率/点数/IFBW、触发源/范围、CONT/SING/HOLD、INIT:CONT、空闲 MAN 下 INIT、测量定义/选择/目录/删除、ASCii,0、SENS:X? 和 CALC:DATA? RDATA。支持正式短名/全名、通道 1 后缀、分号消息、相对路径、单/双引号、指数数值与 Hz/kHz/MHz/GHz。测量名称保留大小写。

SING 的内部路径是手动 Start、一次完整 Trigger、Stop 屏障；OPC 响应前通道进入 HOLD。MAN+CONT 等待 INIT；INIT:CONT OFF 选择手动源，不等同 HOLD。忙时 INIT、中止 ABOR、复位 RST、外触发、组数、校准及二进制格式尚未开放；未校准 S11/S21/S12/S22 已开放。

刺激轴从已提交物理配置生成，不要求先扫描。RDATA 必须来自当前有效 acquisitionRevision 和 run 的完整帧；纯目录更新可即时重新计算已有 IQ，物理计划更新后的旧帧、有限扫描未终结、故障或非有限数据都报告错误，不返回伪造全零曲线。连续扫描下多条独立查询可能跨帧；同轮读取需要单次完成/HOLD 后保持配置和触发不变。

软件预算：单消息 16 KiB/128 单元，待分派消息 32，服务操作队列 32，pending OPC watches 32，仪器错误 FIFO 32，输出 1 MiB。超预算明确拒绝或记录正式错误；没有项目自定义命令。

## 验证

顶层 CMake 开启 BUILD_TESTING 时，`vna_scpi_tests` / `service_scpi` 同时验证服务和协议。实际模拟 DLL 用于单位、接收器数据、消息边界及 10001 点输出；可控设备替身用同步门验证阻塞扫描、OPC 上限、断线收尾以及失败单次保留旧帧但拒绝作为当前数据。

仪器能力类型和默认值在 `instrument_profile.h`，验证行为在 `instrument_profile.cpp`。YAML 由 Server 使用 yaml-cpp 解析，再传值给服务；服务不依赖配置文件或 YAML 类型。HTTP 返回能力只读视图，SCPI MIN/MAX 查询与设置从同一 Profile 取边界。2 端口配置禁止 RAW 第 3/4 端口的源和接收器，4 端口配置允许全部 RAW 波量；两者都支持 2×2 未校准 S 参数。

`instrument_profile` 验证 1/2/4 端口的新服务默认测量、HOLD 和无初始帧，并独立检查默认 S 参数确实经过复比值计算。`service_measurement` 覆盖原位切换、源 2 参考波、旧帧失效、能力/控制权/状态/版本拒绝、设备准备失败回退、满目录更新及排队版本竞争；历史 RAW 和黄金向量用例显式准备自己的测量目录。

`live_measurement` 使用阻塞关卡核对 CONT 中同帧重算、新增源的完整帧边界、旧帧计划与 IQ 深不可变、准备失败仍运行、目录新增/删除不重复下发、CAS/控制者/2 端口能力、MAN armed 无隐式触发，以及物理扫描配置仍要求 HOLD。
