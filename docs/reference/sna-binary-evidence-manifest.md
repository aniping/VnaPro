# SNA 220204 二进制证据清单

## 1. 适用范围

本清单固定 `SRC-SNA-APP-220204` 的本次静态观察对象，并把“对象身份”和“观察报告身份”分开登记：

- 下表十个对象的字节数与 SHA-256 只证明本机分析对象的内容身份；
- 绑定这些对象重新执行 `objdump -p`，仍可在本机复核本清单索引的导入、导出和依赖事实；
- **当前没有归档完整 `objdump` 报告，也没有报告 bundle、报告字节数或报告 SHA-256。** 因此这些事实当前只能标记为 `LocalStaticObservation`，不能声称已具备可移植复核证据。

缺少完整静态报告包登记为 `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001`；缺少官方获取链、厂商签名或官方校验值登记为 `GAP-SNA-SOURCE-PROVENANCE-001`。两者的治理规则、阻断边界和不阻断范围只由 [`DEC-EVIDENCE-PORTABILITY-001`](../架构依据/04-决策与验收映射.md#dec-evidence-portability-001) 定义；本清单只维护本机对象/报告身份和技术采集契约，不把哈希匹配写成官方来源证明。

本清单中的 `VerifiedLocal` 表示“本机对象身份已核对”，`LocalStaticObservation` 表示“取得精确对象后可在本机重跑的窄静态观察”。二者都是证明范围标签，不是兼容性门禁状态；需要可移植证据的门禁当前统一为 `BlockedPendingEvidence`。

本清单不证明文件来源真实性、运行时行为、线程安全、永久阻塞特征、内部领域所有权或厂商源码架构。

记录日期：2026-08-28；对象只读复核日期：2026-08-30。命令工作目录以 VnaPro 仓库根目录为基准。2026-08-30 只读复核时，十个对象的字节数和 SHA-256 均与下表一致，但没有发现可登记的完整报告文件。

本机取得的 SNA 对象集不随本仓库复制，因此仅克隆 VnaPro 仓库不能独立复核二进制内容。要关闭 `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001`，portable evidence bundle 必须同时携带经许可分发的十个原文件、逐文件身份清单、十份未筛选的完整 `objdump -p` 报告及其报告清单；缺其中任一项都保持 `BlockedPendingEvidence`。这不自动关闭 `GAP-SNA-SOURCE-PROVENANCE-001`，后者还需要独立的官方来源采集链。

本机分析根目录：`../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder`。

## 2. 文件身份

| 相对路径（相对 VnaPro 根目录） | 字节数 | `file` 识别结果 | SHA-256 |
| --- | ---: | --- | --- |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/Snau.exe` | 9,605,120 | PE32+ GUI executable, x86-64 | `69fcbde60ef7102074c7adadc86610a9b6921e222e588edde1a40de05a224bc1` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/ParserDllu.dll` | 67,072 | PE32+ GUI DLL, x86-64 | `d7ba71cd9947459e50eff8b43987db3bcc91e0a5ea815d3b38f6e0afc6040b0f` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/EcalDllu.dll` | 2,691,072 | PE32+ GUI DLL, x86-64 | `e6ab6bce812fba08181cb02a81b414eae5d48d90f191f443a06566075e713fcf` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/FixSimDllu.dll` | 3,899,392 | PE32+ GUI DLL, x86-64 | `10cacd68e765602f1bc47252c32359bacb4d12291fc2a8fb74c7e4829d84cbd9` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/SnpFileDllu.dll` | 4,065,792 | PE32+ GUI DLL, x86-64 | `856253dee46ce7bdeed410ad32802eaaa4ed0a0904ad1f0bb91c4eb8027d9b6f` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/Dspu.dll` | 159,232 | PE32+ console DLL, x86-64 | `918f12f49e8de52f1c924f131d52cb25bb9911ea40d042647b78601f76c2e7b4` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/TimeDomainTranu.dll` | 111,616 | PE32+ GUI DLL, x86-64 | `6ab53b7bb7e8d3f9d91f10a862194eeeec9dbe4e250fa0a4aa2d14d53e170190` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/LsbGraphu.dll` | 710,144 | PE32+ GUI DLL, x86-64 | `e96d51fe305987a80296a8998806ffc8a65b9540df56b2f4a369db70852d3b3e` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/Gpibu.dll` | 107,520 | PE32+ console DLL, x86-64 | `5730b7c2bec63ec53c9d1e929aee161f5a9ad84963e2e272ef331f19268936c4` |
| `../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder/ErrorMessageProcessu.dll` | 28,160 | PE32+ GUI DLL, x86-64 | `f6da3d14d2e7eafeaeeef83d6d108c07078ddddfe15d9596f6e6d589286aa82c` |

## 3. 可重复命令与环境

本次环境：macOS 26.6.1（Build 25G76），Darwin 25.6.0 arm64，Xcode 26.2（Build 17C52）；`shasum` 6.02，`file` 5.41，Apple LLVM `objdump` 17.0.0。`strings` 来自 Xcode 26.2 的 `/usr/bin/strings`，该程序不提供 `--version`；本次结论不依赖 `strings` 输出。

逐文件校验命令：

```zsh
sna_root='../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder'
sna_files=(
  Snau.exe ParserDllu.dll EcalDllu.dll FixSimDllu.dll SnpFileDllu.dll
  Dspu.dll TimeDomainTranu.dll LsbGraphu.dll Gpibu.dll ErrorMessageProcessu.dll
)
for sna_name in "${sna_files[@]}"; do
  /usr/bin/shasum -a 256 "$sna_root/$sna_name"
  /usr/bin/stat -f '%N|%z bytes' "$sna_root/$sna_name"
  /usr/bin/file "$sna_root/$sna_name"
done
```

导入/导出本机复核命令：

```zsh
sna_root='../VNA/local-analysis/SNA_PCIE_220204_TRG_EXT/AppFolder'
/usr/bin/objdump -p "$sna_root/Snau.exe"
for sna_name in \
  ParserDllu.dll EcalDllu.dll FixSimDllu.dll SnpFileDllu.dll Dspu.dll \
  TimeDomainTranu.dll LsbGraphu.dll Gpibu.dll ErrorMessageProcessu.dll; do
  /usr/bin/objdump -p "$sna_root/$sna_name"
done
```

上述命令只是可重跑方法，**命令文本和对象哈希不能替代报告哈希**。终端截取、`grep`/`rg` 过滤结果、聊天记录和仅在管道中计算但未保留的摘要，都不能登记成完整报告。

观察事实、证明边界和关联决策的唯一正文是[《SNA 逆向证据》](../架构依据/02-SNA逆向证据.md)；本清单不复制这些结论。

### 3.1 关闭 GAP 的采集清单

采集工作必须在经批准的全新空目录完成，并保留原始输出；本次文档整改没有执行这些写入步骤。

1. 再次校验十个源对象的相对路径、字节数和 SHA-256，任一不一致就停止采集，不沿用本清单的 Evidence ID。
2. 固定 `LC_ALL=C`，记录操作系统、处理器、`objdump` 的绝对路径和版本、完整 argv、采集时间与操作者；工具或对象变化必须产生新的 Bundle ID。
3. 对十个对象逐一执行一次 `/usr/bin/objdump -p <对象>`。每个对象单独保存未经筛选、未经截断的 stdout 和 stderr，并记录退出码；退出码非零不能进入 Passed bundle。
4. 对每份 stdout、stderr 分别记录文件相对路径、字节数和 SHA-256。零字节 stderr 也必须记录为零字节文件及其真实哈希，不能省略。
5. 在 Evidence ID 索引中增加 `ReportId + ObservationLocator`，把每项观察定位到报告内的导入名、导出名或修饰符号；定位信息不能代替完整报告。
6. 生成 bundle manifest，记录全部对象和报告记录；再记录 manifest 自身的字节数与 SHA-256，并由独立复核者验证对象哈希、十次退出码、报告完整性和 Evidence ID 定位。
7. 只有原文件分发/缓存权限边界、十份报告和 manifest 全部可随证据包取得时，才把 `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` 改为 Closed。仅补一份报告、只补摘要或只补哈希都不能关闭。

### 3.2 未来报告 manifest 的固定结构

每个对象必须恰有一条报告记录；以下是字段契约，不表示这些报告当前已经存在：

| 字段 | 含义 | 当前值 |
| --- | --- | --- |
| `BundleId` / `ReportId` | 唯一绑定一次采集和一个源对象 | `Pending` |
| `SourceObjectPath` / `SourceObjectBytes` / `SourceObjectSha256` | 与第 2 节对象身份逐字段一致 | 对象字段已知，报告尚未绑定 |
| `ToolPath` / `ToolVersion` / `EnvironmentDigest` | 生成工具和环境身份 | `Pending` |
| `Argv` / `Locale` / `CapturedAt` / `Collector` | 精确命令与采集上下文 | `Pending` |
| `StdoutPath` / `StdoutBytes` / `StdoutSha256` | 未筛选完整 `objdump -p` 报告 | `Pending` |
| `StderrPath` / `StderrBytes` / `StderrSha256` | 未筛选完整 stderr，允许真实零字节文件 | `Pending` |
| `ExitCode` | 必须为 `0` | `Pending` |
| `ObservationLocators` | 该报告支撑的 Evidence ID 与精确观察定位 | `Pending` |
| `Reviewer` / `ReviewedAt` / `ReviewReceipt` | 独立复核记录 | `Pending` |

bundle 顶层还必须记录十个 `ReportId` 的规范顺序、许可或内部缓存边界、manifest 文件字节数和 manifest SHA-256。自引用字段不能写入被自身哈希覆盖的 manifest 正文，可放在同级签名/receipt 文件中，并分别登记其身份。

## 4. 证据能力与哈希索引

Evidence ID 只在这里索引到参与本机复核的已哈希对象；观察和边界请跟随最后一列的 canonical 记录。下表所有 EV 仍可证明**绑定精确对象哈希、在本机重跑得到的窄静态事实**，但没有任何一项已取得可移植报告身份。

| Evidence ID | 已哈希的复核对象 | 当前可用证明力 | 完整报告状态 | Canonical 事实与边界 |
| --- | --- | --- | --- | --- |
| `EV-REV-MAIN-IMPORT-001` | `Snau.exe` 及表中九个 DLL | `LocalStaticObservation`：静态导入事实 | `Pending`：`GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-main-import-001) |
| `EV-REV-PARSER-001` | `ParserDllu.dll` | `LocalStaticObservation`：导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-parser-001) |
| `EV-REV-ERROR-001` | `ErrorMessageProcessu.dll` | `LocalStaticObservation`：导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-error-001) |
| `EV-REV-ECAL-001` | `EcalDllu.dll` | `LocalStaticObservation`：导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-ecal-001) |
| `EV-REV-FILE-001` | `SnpFileDllu.dll` | `LocalStaticObservation`：导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-file-001) |
| `EV-REV-TIME-001` | `TimeDomainTranu.dll` | `LocalStaticObservation`：导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-time-001) |
| `EV-REV-DRIVER-SEAM-001` | `Dspu.dll`、`Gpibu.dll`、`Snau.exe` | `LocalStaticObservation`：导入/导出 seam | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-driver-seam-001) |
| `EV-REV-FIXSIM-COUPLING-001` | `FixSimDllu.dll` | `LocalStaticObservation`：导出耦合反例 | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-fixsim-coupling-001) |
| `EV-REV-GRAPH-COUPLING-001` | `LsbGraphu.dll`、`TimeDomainTranu.dll` | `LocalStaticObservation`：导入/导出耦合反例 | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-graph-coupling-001) |
| `EV-REV-MAIN-COUPLING-001` | `Snau.exe` 及表中九个 DLL | `LocalStaticObservation`：静态依赖反例 | `Pending`：同一 GAP | [查看](../架构依据/02-SNA逆向证据.md#ev-rev-main-coupling-001) |

### 4.1 当前允许与禁止的使用方式

当前允许：在能取得第 2 节精确对象、重新校验对象哈希并重跑命令的本机审计中，把上述 EV 作为候选 seam 或耦合反例的窄输入。

当前禁止：

- 声称本仓库或当前 portable bundle 已携带可独立复核的 SNA 逆向报告；
- 用上述 EV 通过发布、交付、合规或其他要求完整证据包的门禁；
- 把对象哈希、命令文本或截断输出写成报告字节数/报告哈希已经存在；
- 从静态导入/导出外推运行时顺序、线程安全、所有权、故障语义或 ZNB/SNA 行为兼容。

除上表索引项外，不得仅凭 DLL 名、导出名或 C++ 修饰名新增架构结论。需要增加 seam/依赖结论时，必须在 canonical 文件补 Evidence ID、完整原始报告、报告字节数、报告哈希、ObservationLocator 和该结论不能证明的边界；本清单再补对象/报告身份和跳转。在 `GAP-SNA-OBJDUMP-REPORT-BUNDLE-001` 关闭前，不新增宣称已具备可移植复核能力的 `EV-REV-*`。
