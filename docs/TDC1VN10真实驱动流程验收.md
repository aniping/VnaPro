# TDC1VN10 真实驱动流程检查与验收

日期：2026-10-15。范围为正式 VN10 设备适配器、采集核心、仪器服务及 HTTP/SCPI。保留 VN08 旧接口和两个生产模拟驱动实现；本轮未连接真实板卡。

## 已确认的驱动合同

- 返回值 0 表示成功；`4294967295`（`0xFFFFFFFF`，有符号解释为 −1）是已确认的通用失败。其他非零值按调用步骤和原数值报告，不套用模拟器错误枚举。
- 频率使用 `uint64_t[]` Hz、IFBW 使用 `uint32_t[]` Hz；`DrvSetPowerList(double[], uint32_t)` 使用 dBm，闭区间 `[-120,100]`，允许负数与小数。IFBW 函数精确签名为 `DrvSetIfBwList(uint32_t[], uint32_t)`。
- 原生端口为 0～3，仪器逻辑端口为 1～4，适配器进行明确转换。
- 工作端口只决定激励源块的数量。输出按工作源端口分块，每源包含全部请求频点，每点固定四接收端口的 A实/A虚/B实/B虚，共 16 个 double。
- `DrvGetSweepData` 的 `dataLen` 已确认为 double 元素容量，按 `请求频点数 × 工作端口数 × 16` 传递；两工作端口、两频点传 64，不乘以 `sizeof(double)`。
- `DrvSetRfPowerOn(0)` 只关闭端口，不停止扫描。运行期不卸载驱动，进程退出时由系统释放。

## 正式调用流程

| 阶段 | 行为 | 异常或资源策略 |
| --- | --- | --- |
| 初始化 | 绑定九个 VN10 函数，HwInit 成功后显式 RF OFF | 缺符号或初始化失败保留原始错误；模块保持驻留，不调用旧 VN08 的 IDN/Rx/FastSweep 函数 |
| 配置 | 准备会话拥有的频率、功率、IFBW、工作端口数组，四次 SET 后组帧 | 分配失败发生在设备变更前；组帧失败仍持有旧成功列表和本次候选，避免悬空指针 |
| 开始 | 开端口，单次 Start 发起当前工作源的扫描 | Start 已尝试后，未成功读回整帧就不能确认扫描结束 |
| 取数 | 按最多 64 个频点分段，按请求源顺序拆入独立端口缓冲 | 全部 Get 成功才发布不可变完整帧；部分输出或错误不替换上一帧 |
| HOLD | 核心 FIFO 等当前整帧完成，停止后续 Start | 保持 RF，不把软件 HOLD 当作硬件取消 |
| 关闭 | 拒绝新操作，排空已接受任务、等在途读取结束，再 RF OFF、join 主机 worker | 不卸载 DLL；借用列表和取数缓冲到进程退出才释放。RF OFF 失败或扫描结束未知返回失败 |
| 恢复 | 没有未完成扫描时，在同一库句柄上重新初始化并恢复最后成功计划 | Start/Get 失败且结束未知时拒绝恢复，不再发 Init/SET/Start；保留旧帧和库 |

RF OFF 不能中止正在阻塞的 Get。供应接口没有取消、完成查询、超时或释放入口，因此原生调用永久不返回时，不能承诺软件 Close 限时完成。软件不会强杀工作线程或卸载其执行中的代码。

## 公司现场初始化与下发诊断

将整个 `release` 复制到公司环境，编辑程序旁的 `vnapro.yml`，使用真实库对应的 `driver.type: hardware`、`driver.board: tdc1vn10` 与实际 DLL 文件名。日志记录的是适配器实际传给驱动的数组，IFBW 元素应显示 `uint32_t bytes=4`；旧 uint64 缓冲被新驱动按 4 字节读取时可能出现 `1000,0,1000,0` 交错，不能仅检查首项。

默认 INFO 已包含 DLL 路径、初始化调用、配置尝试号、四组数组摘要以及完整原生端口列表。SET 返回 void 只记为调用返回，其有效性由随后 Download 判断；日志不会将其称为配置成功。每个调用均先记 `event=begin`，再调用 DLL，返回后记录 `event=returned`。Download 的状态行保留 `status_u32`、`status_i32` 和 `status_hex`；`result` 只将 0 标为 SUCCESS、4294967295 标为 FAILURE，其他非零码标为 UNRECOGNIZED_NONZERO，并继续按失败处理。未返回的调用会留下 begin，没有虚构的完成记录。

现场已确认实际返回 `UINT32_MAX`，即 `4294967295`（`0xFFFFFFFF`、有符号解释为 −1），表示通用失败。此前转述的数值是笔误，不作为厂商错误码或故障原因的依据；具体失败原因仍需结合下发参数与驱动现场定位。

如需完整参数，可将现有 log 配置改为以下级别与文件设置后重启，其他原有日志设置可保留：

```yaml
log:
  level: DEBUG
  console:
    enabled: true
  file:
    enabled: true
    path: logs/vnapro.log
    maxSizeBytes: 10485760
    maxFiles: 5
    maxRecordBytes: 65536
  flush:
    level: DEBUG
```

DEBUG 在 `DrvDataFrameDownload event=begin` 之前输出频率、功率、IFBW 和端口的完整值，每条至多 16 项；用同一 `config` 和 `name` 的 `offset/count` 还原数组，最后一块可能不足 16 项。double 用 17 位有效数字保留，uint64 频率保持十进制精度。即使维持 INFO，Download 失败时 ERROR 也会补完整参数。不要将 `maxRecordBytes` 调得过小，否则统一日志截断规则仍会生效。

日志默认在程序旁的 `logs/vnapro.log`，包含轮转的 `.1.log` 等文件。可先从日志筛选 `[VN10]`，但排查时应带走同一时间段的完整日志及实际 `vnapro.yml`，保留调用前后顺序。诊断只发生在初始化、配置及射频生命周期边界；不在成功的 Start/Get 或逐帧扫描路径记录数组。`config` 是当前会话的尝试号，不等同于采集配置版本。

## 检查发现与修复

1. 原四个 SET 数组是局部变量，组帧返回后即释放，隐含驱动会复制数组。现在由设备会话持有；独立保留指针型测试 DLL 已复现旧路径 `DrvStartSweep returned 7901`，修复后通过。
2. 原 Shutdown 无论 RF OFF 成败都卸载库，并把模拟器的取消/回收线程行为推广给真实接口。现在统一驻留至进程退出；加载绑定失败路径也不主动卸载。
3. 原失败取数后可以立即重新初始化。现在只有全部源/频点成功读回才清除扫描未完成标志，状态未知时拒绝重新初始化。
4. 原初始化依赖模拟器默认 RF 关闭。现在 HwInit 成功后显式关闭端口。
5. 服务不再隐式依赖模拟器功率默认值：`instrument.sweep.defaultPowerDbm` 启动冻结，扫描计划用 `powerDbm: vector<double>` 逐点明确下发。旧 `defaultPowerRaw` 仅兼容整数 0，非零旧编码需明确改写为 dBm；两个配置键不能共存。
6. 刺激轴读取已成功提交的计划，计划覆盖判断包含功率，范围错误不再标为 mock。旧 VN08 的 9N 表准备逻辑保留兼容；VN10 下发不消费该表。

实际采样值始终来自 Get 返回的 IQ。S 参数按同一源帧计算 `Sij = Bi / Aj`；硬件路径不读取噪声模式、种子、幅度、黄金向量，也不将模拟器身份作为准入条件。

## 独立验收

探针是独立测试 DLL，复写供应头九个签名，不包含生产模拟器模型、状态机或错误枚举。它采用保留 SET 指针的实现，并记录原生调用顺序、线程、分段参数和故障；测试用 731 故障码与独立 IQ 编码验证主机行为。

| 用例 | 实际覆盖 |
| --- | --- |
| `core_hardware_board_lifecycle` | 两/四源和反向源序，130 点的 64＋64＋2 拼接，全部四接收器 A/B 分量；连续不越过在途 Get；Close 已进入准入屏障后仍等待 Get；失败保留完整帧、拒绝错误恢复、空闲恢复、RF OFF 原码及运行期不卸载 |
| `core_board_diagnostics` | INFO 调用顺序与实际参数摘要、DEBUG 完整值重建、UINT32_MAX 及合成未知码失败上下文、失败保帧、初始化失败、两帧采集不增加 VN10 日志；4000 点完整分块与 uint64 精度 |
| `hardware_board_acquisition` | 正式 hardware 服务，2/4 端口全 S 矩阵、原始 A/B 逐值核对、HTTP/SCPI 同帧比值、CONT/HOLD、非法模拟配置与环境隔离 |
| `instrument_profile` / `server_config` / `instrument_power_config` | 负数、小数及 `[-120,100]` 两端功率，拒绝非有限值和越界值；改点数后仍按冻结配置下发；配置下发阻塞/失败期间的频轴一致性；旧功率键仅兼容整数 0 |

服务脚本终止子进程只用于清理；有序关闭的证据来自直接调用 Core.Close 的门闩用例，不能把进程终止当作关闭成功。

验证命令：

```powershell
cmake --build cmake-build-debug -j 6
ctest --test-dir cmake-build-debug -j 4 --output-on-failure
```

此前基线已完成 75 项 CTest 和前端 439 项测试。接口修正增加供应头独立合同、double 功率及配置/校准回归；本轮补充 IFBW 数组宽度、初始化和配置诊断回归。实际通用失败码只能确认调用失败，不能据此认定具体参数或硬件故障。验收证明软件调用、数据与资源合同，不代替厂商 DLL 和真实板卡的采集验收。
