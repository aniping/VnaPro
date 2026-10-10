# 仪器服务与 SCPI 引擎

`vna_service` 只使用 `vna_core` 公开接口；`vna_scpi` 将已登记的 Keysight N52xxB 命令转换为服务操作，不访问驱动。当前实现单通道，SCPI 与 Web 共用服务及独占控制权，兼容范围为已登记的 PNA 命令子集。

按职责组织类型：instrument_settings.h 放置 InstrumentSettings、ConfigPatch 与配置关联的 TriggerSource/SweepMode；service_task.h 将内部 ServiceTask 与 Kind/Stage 放在一起。Measurement、OperationResult、InstrumentError、DataResult、InstrumentSnapshot 各自有独立的测量定义、异步结果、错误队列、查询数据和状态快照职责，保留独立头文件。instrument_service.h 声明服务入口并包含所需类型。Measurement::Parameter 的行为在 src/measurement.cpp，InstrumentService 直接管理控制归属、设置、错误寄存器和任务 FIFO。ServiceTask 按值存储，Stage 标明配置、触发和停止屏障；每个状态推进函数独立可读。测试受控设备和 Fixture 各自独立，通过 Core 的公开所有权注入构造函数接入；长实现位于对应 cpp，由 vna_service_test_support 链接到测试目标。

## 生命周期和线程

进程拥有 `AcquisitionCore`。初始化成功后构造 `InstrumentService(core)`，在监听前调用 `BootstrapMockProfile(error)`；这是唯一允许阻塞等待的服务方法。它检查缓存身份必须以 `VnaPro,NativeSimulator,` 开头。服务必须先于核心销毁。

运行期间，服务、引擎和状态寄存器都在同一个 executor 调用。`ScpiEngine(service)` 自动尝试取得独占控制权，`HasControl()` 表示是否成功。`SubmitLine()` 接受不含 LF 的完整程序消息；返回 true 表示整条消息已受理，不能据此认为设备操作已经完成。

网络每约 5 ms 调用 `SetTransportOutputPending(...)` 和 `Poll()`，通过 `TakeOutput()` 取得完整响应消息，再处理 socket 的部分发送。多个查询响应单元用分号分隔，消息以 LF 结束；写命令没有 ACK。数组每轮最多编码 256 个值，每次最多分派 32 个单元，采集 future 从不在网络循环中阻塞等待。

断开调用 `Disconnect()` 后继续 `Poll()`。已经受理的完整程序消息、扫描和屏障继续推进，响应丢弃；`CanReleaseControl()` 为 true 后才能销毁引擎并接纳新控制连接。未成为完整消息的 socket 输入由网络层丢弃。持续扫描本身不阻止连接收尾，也不因断开而自动停止。核心关闭和 join 由进程拥有方在网络循环之外执行。

仪器错误 FIFO 和状态寄存器由服务持有，跨引擎/连接保留。Web 写入口使用服务签发的会话 token 和相同写授权；释放时排空已接受有限操作，再交还控制权。HTTP 会话合同见[服务设计](../docs/网分服务与远程控制设计.md)。

## 当前模拟配置

- 默认频率 1 MHz–100 MHz，201 点，IFBW 1 kHz，测量 `DefaultA` 为 `a1,1`，初始 HOLD。
- 仅模拟合同规定一个 frequencyRaw 计数等于 1 Hz，IFBW raw 采用相同整数；9N 表全零且为 opaque 数据，不代表真实功率、增益或校准表。
- 频率范围 1 Hz–1 GHz，端点按整数 Hz 回读；端点交叉保留旧 span，联动另一端点，联动后超范围则拒绝整个候选。
- 点数 2–10001；IFBW 有效值为 10、100、1000、10000、100000 Hz，向上取适用值。配置只允许 HOLD 且无未完成有限测量；执行时再次校验状态和 expectedRevision。
- 配置候选提交期间，服务的直接测量选择和触发范围修改报告状态冲突，防止整份候选提交覆盖已成功修改的字段；两者也必须持有控制权。有限测量期间仍允许选择已定义的曲线。
- 接收器映射仅采用模拟器明确的 IQ 布局，每点四端口、每端口 A.real/A.imag/B.real/B.imag。非比值参数接受 `a1..a4` 或 `b1..b4` 和源端口 1..4；接收器字母大小写不敏感，测量名称保留大小写。

以上范围与换算是软件测试 profile，不能推定为 Keysight 或真实板卡的能力。

## 已实现范围

支持设计白名单中的身份、错误、ESE/ESR/SRE/STB、OPC/WAI、线性频率/点数/IFBW、触发源/范围、CONT/SING/HOLD、INIT:CONT、空闲 MAN 下 INIT、测量定义/选择/目录/删除、ASCii,0、SENS:X? 和 CALC:DATA? RDATA。支持正式短名/全名、通道 1 后缀、分号消息、相对路径、单/双引号、指数数值与 Hz/kHz/MHz/GHz。测量名称保留大小写。

SING 的内部路径是手动 Start、一次完整 Trigger、Stop 屏障；OPC 响应前通道进入 HOLD。MAN+CONT 等待 INIT；INIT:CONT OFF 选择手动源，不等同 HOLD。忙时 INIT、中止 ABOR、复位 RST、外触发、组数、S 参数、校准及二进制格式尚未开放。

刺激轴从已提交物理配置生成，不要求先扫描。RDATA 必须来自当前有效 revision 和 run 的完整帧；有限扫描未终结、旧帧、故障或非有限数据都报告错误，不返回伪造全零曲线。连续扫描下多条独立查询可能跨帧；同轮读取需要单次完成/HOLD 后保持配置和触发不变。

软件预算：单消息 16 KiB/128 单元，待分派消息 32，服务操作队列 32，pending OPC watches 32，仪器错误 FIFO 32，输出 1 MiB。超预算明确拒绝或记录正式错误；没有项目自定义命令。

## 验证

顶层 CMake 开启 BUILD_TESTING 时，`vna_scpi_tests` / `service_scpi` 同时验证服务和协议。实际模拟 DLL 用于单位、接收器数据、消息边界及 10001 点输出；可控设备替身用同步门验证阻塞扫描、OPC 上限、断线收尾以及失败单次保留旧帧但拒绝作为当前数据。
