# 架构依据 02：SNA 逆向证据

> 上一篇：[商用品行为证据](./01-商用品行为证据.md) · [返回证据入口](../架构依据与取舍矩阵.md) · 下一篇：[开源机制参考](./03-开源机制参考.md)

本文件唯一维护本机取得的 SNA 哈希对象集和本地迁移资产的使用边界。静态导入、导出和依赖只能证明物理接缝存在，不能证明这些对象的官方来源、职责归属或运行时语义。

## 1. 分析对象

### `SRC-SNA-APP-220204`

**来源**

本机取得的 SNA 哈希对象集（官方来源待证）

**本机位置**

`../../../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder`

**可移植引用与完整性**

- 未确认公开官方下载页；
- 未取得厂商签名、官方校验值或完整安装包采集链，故不得称为“原始官方应用包”；
- 十个分析文件的路径、大小、逐文件 SHA-256、工具环境与复核命令见[二进制证据清单](../reference/sna-binary-evidence-manifest.md)。
- 2026-08-30 只读复核确认十个本机对象的字节数和 SHA-256 仍与清单一致；
- 本仓库不携带这些对象，也没有归档完整 `objdump` 报告、报告 bundle、报告字节数和报告 SHA-256；因此仅克隆本仓库不可独立复核。

**当前证据状态**

`GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` 是证据可移植性缺口；`GAP-SNA-SOURCE-PROVENANCE-001` 是来源真实性缺口。两者由 [`DEC-EVIDENCE-PORTABILITY-001`](./04-决策与验收映射.md#dec-evidence-portability-001) 统一规定阻断与不阻断范围。当前只验证了本机对象字节身份，清单中的静态导入/导出事实可绑定精确对象重跑，但报告工件和尚未建立的官方采集链都不能由对象哈希替代；因此本页全部 `EV-REV-*` 仍是 `LocalStaticObservation`，需要可移植或官方来源证据的放行仍是 `BlockedPendingEvidence`。

关闭条件、逐对象采集步骤和未来报告 manifest 字段只在[《SNA 220204 二进制证据清单》3.1—3.2](../reference/sna-binary-evidence-manifest.md#31-关闭-gap-的采集清单)维护。在完整报告、字节数、哈希和独立复核 receipt 登记前，不得把对象哈希或命令文本当成报告身份。

### `SRC-LOCAL-SNA-MIGRATION`

**项目或资产**

[本机迁移目录 `../../../SNA/src`](../../../SNA/src)

**固定版本或可移植引用**

- 现有工程迁移资产；
- 当前目录未纳入 Git，也未固定树哈希或 portable fallback；因此明确降级为 `NonEvidenceLead`（迁移线索），不属于原产品证据，不能单独支撑任何 Decision ID

**用途与边界**

- 盘点可迁移接口和缺口；
- 只能生成待重新验证的迁移 backlog，不得反向证明 SNA 的实现、语义或架构

## 2. 逆向观察

### 2.1 本机 SNA 哈希对象能证明什么

以下观察均绑定 `SRC-SNA-APP-220204` 的[逐文件哈希与本机复核命令](../reference/sna-binary-evidence-manifest.md)，不是只凭文件名作出的推测。它们保留的证明力是：在取得精确哈希对象后，可重跑验证下列窄静态事实；它们当前**不是**已归档、可移植的逆向报告证据。

| 使用场景 | 当前状态 | 处理规则 |
| --- | --- | --- |
| 本机对象身份 | `VerifiedLocal` | 十个对象的大小和 SHA-256 已匹配清单 |
| 本机静态导入/导出观察 | `LocalStaticObservation` | 以下十项 EV 可作候选 seam/耦合反例的窄输入 |
| 仅克隆 VnaPro 后离线复核 | `BlockedPendingEvidence` | 缺对象文件和完整报告 bundle，不得宣称可复核 |
| 发布或交付可移植证据包 | `BlockedPendingEvidence` | `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` 关闭前不得放行 |
| SNA 运行时行为或兼容声明 | 不在这些 EV 的证明范围 | 仍需独立用户手册证据和真机 transcript，不能由静态表替代 |

<a id="ev-rev-main-import-001"></a>

#### `EV-REV-MAIN-IMPORT-001`

`Snau.exe` 静态导入 `EcalDllu`、`FixSimDllu`、`SnpFileDllu`、`Dspu`、`TimeDomainTranu`、`LsbGraphu`、`Gpibu`、`ParserDllu`、`ErrorMessageProcessu`。关联 `DEC-LAYER-PURECORE-001`，仅作“物理 DLL 边界不能直接当领域边界”的反例输入。

<a id="ev-rev-parser-001"></a>

#### `EV-REV-PARSER-001`

`ParserDllu` 有 CommandTree、Parser FIFO、OPC wait flag 和 parser error 接口。关联 `DEC-SCPI-ORDER-001`，只证明命令解析边界存在，不支持 SCPI 顺序语义。

<a id="ev-rev-error-001"></a>

#### `EV-REV-ERROR-001`

`ErrorMessageProcessu` 有错误链的初始化、查询并删除、加锁和解锁接口。关联 `DEC-STATUS-001`，只证明错误处理边界存在，不支持 ESR/STB 语义或责任归属。

<a id="ev-rev-ecal-001"></a>

#### `EV-REV-ECAL-001`

`EcalDllu` 有标准信息、ECal 路径和误差项计算接口。关联 `DEC-CAL-OWN-001`，只证明校准相关物理边界存在，不支持据此推断工作流、求解器和存储库应怎样切分。

<a id="ev-rev-file-001"></a>

#### `EV-REV-FILE-001`

`SnpFileDllu` 有 Touchstone 读、写和释放接口。关联 `DEC-PERSIST-001`，只证明 Touchstone 文件边界存在，不支持原子保存、召回事务或崩溃恢复。

<a id="ev-rev-time-001"></a>

#### `EV-REV-TIME-001`

`TimeDomainTranu` 有 FFT/Chirp-Z、时域变换和时域门计算。关联 `DEC-XFORM-ORDER-001`，只证明变换边界存在，不支持据此确定处理链位置或任意改变处理顺序。

<a id="ev-rev-driver-seam-001"></a>

#### `EV-REV-DRIVER-SEAM-001`

`Dspu` 与 `Gpibu` 分别暴露 DSP/PCI、GPIB I/O 接口，并被 `Snau.exe` 静态导入。关联 `DEC-DRIVER-ISOLATION-001`，只证明设备输入输出边界存在，不证明驱动会永久阻塞或原产品使用进程隔离。


这些事实的证明力止于清单登记的 seam 和静态依赖。即使未来补齐报告 bundle，也只提升可移植复核能力，不会提升事实类型：它们可以作为划分候选 seam 的输入，却不证明服务应归哪一层、内部依赖方向、调用时序、线程安全、故障模式或领域所有权；VnaPro 的进程隔离、纯 Core 和 Application ports 都仍是需要单独验收的架构决策。

### 2.2 本机哈希对象中的耦合反例

<a id="ev-rev-fixsim-coupling-001"></a>

#### `EV-REV-FIXSIM-COUPLING-001`

`FixSimDllu` 同时导出 `FIXSIM_Dlg*` 界面入口和 `FIXSIM_DeembCompute`、`FIXSIM_EmbedCompute` 等计算入口。关联 `DEC-LAYER-PURECORE-001`，作为拒绝照搬界面与计算物理耦合的反例。

<a id="ev-rev-graph-coupling-001"></a>

#### `EV-REV-GRAPH-COUPLING-001`

`LsbGraphu` 导出表中同时可见 Channel/Window/Line/Marker 类符号，导入表中可见 `TimeDomainTranu.dll`；这里只登记这两个可重复观察。关联 `DEC-LAYER-PURECORE-001`，作为拒绝照搬宽职责图形 DLL 边界的反例，不外推完整依赖图。

<a id="ev-rev-main-coupling-001"></a>

#### `EV-REV-MAIN-COUPLING-001`

`Snau.exe` 导入表直接列出 2.1 登记的多个算法、文件、输入输出、命令解析、错误处理和图形 DLL；只证明静态依赖存在。关联 `DEC-LAYER-PURECORE-001`，不将主程序导入图当作应照搬的分层图。

因此“拆成 DLL”不等于职责合理。本项目拒绝复制 `FixSim`/`LsbGraph` 的物理边界，改用纯 Core、Application use case 与 UI Adapter 分离。

### 2.3 `/cc/SNA` 现有工程迁移资产的使用边界

`SRC-LOCAL-SNA-MIGRATION` 不是原 SNA 源码或原产品证据。可评估迁移的工程资产包括版本化 C ABI、动态 Hardware Host、Application ports、completed immutable snapshot、operation generation/fence，以及与本节已登记二进制观察对应的 Parser/Error 物理 seam；是否迁移必须重新通过 VnaPro 契约和测试，不能把迁移资产中的设计反推为商用品事实。

不可作为 VnaPro 上层依据的现状：

#### `AcquisitionSnapshot{ChannelNumber, vector<complex>}` 被直接作为 `CALC:DATA? SDATA`

**语义缺口**

- 缺 Run/Plan/Axis/Source/Receiver/Schema/AccessPoint；
- 不符合商用品 SDATA 访问点

**对 VnaPro 的提醒**

ResultRepository 必须以 ProcessingAccessPoint、OutputSchema 和完整 provenance 查询

#### `RequestImmediateAcquisition(channelNumber)` 映射 hardware logical endpoint

**语义缺口**

- 商用 Channel 被泄漏到 HAL；
- 不能编译多个 source partial

**对 VnaPro 的提醒**

HAL 接 Recipe/Step，不接 ChannelId

#### Parser 支持单命令/少量参数并逐行立即执行

**语义缺口**

不能承载 Segment list、quoted name、binary block、CalSet 多参数，或 ZNB 的命令识别顺序、Setting data set 到 terminator/DCL 才请求硬件应用及 WAI/OPC 同步语义

**对 VnaPro 的提醒**

无副作用 syntax UnitPlan + ordered command/effect cursor 先于扩命令

#### ErrorQueue/Status 未与 CommandSession 闭环

**语义缺口**

命令失败不能正确进入 ESR/STAT/Error FIFO

**对 VnaPro 的提醒**

状态、错误、操作票据由 Application 统一拥有

这部分只作为现有迁移资产的代码审计，不增加任何未在 2.1/2.2 登记的原产品 seam、依赖或行为结论，也不是对 VnaPro 仓库已有三层的代码结论。`SRC-LOCAL-SNA-MIGRATION` 的 `NonEvidenceLead` 状态与两个 SNA 证据 GAP 相互独立：补齐对象报告不能把迁移目录变成原产品证据，固定迁移目录也不能补齐报告或官方来源采集链。
