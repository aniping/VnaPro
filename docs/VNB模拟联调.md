# VNB 四端口软件联调

本流程用于独立 `vna_server.exe` 和 VN10 模拟采集驱动，不代表真实硬件带宽、校准溯源或测量精度。四端口 TOSM 的标准采集与联合求解见[校准接入与使用](校准接入与使用.md)。

## 启动与诊断夹具

先构建 `cmake --build cmake-build-debug --target vna_server -j 6`，把 `release` 复制到独立部署目录，将 [vnb-software-40ghz.yml](../config/vnb-software-40ghz.yml) 复制为该目录的 `vnapro.yml`，再无参数启动其中的 `vna_server.exe`。程序只读取自身目录配置。该配置明确使用 simulator：P1～P4、10 MHz～40 GHz、800 点、1 kHz、0 dBm，启动 HOLD；默认发布配置仍为 8 GHz。CableTest 的 TDR 分析重采样到 10 MHz 网格，因此这里从 10 MHz 起扫，供其补 DC 后连续成网格；原始采样间隔不等于分析网格间隔，TDR 前置校验由 CableTest 执行。

隔离部署位于 `reference/analysis/vnb-software-e2e-20261016/deployment`，SCPI 为 `127.0.0.1:55025`，HTTP 为 `http://127.0.0.1:58080`。这些端口与原默认 5025/8080 分开。

服务就绪且无其他控制者时，在仓库根目录执行：

```powershell
python scripts/vnb_simulation_calibration.py --http-url http://127.0.0.1:58080 --name SIMULATION_ONLY_FOUR_PORT_DIAGNOSTIC --output reference/analysis/vnb-software-e2e-20261016/deployment/simulation-calibration.json
```

脚本只允许真实回读为 `NativeSimulator` 的四口后台及当前 0 dBm 刺激，按实际能力校验频轴，不改能力。它通过正式 HTTP 完成 HOLD、一次真实采集、非单位诊断系数导入、选择、启用、同帧 16 项数值核对和服务导出，再清理临时测量定义并释放控制权。每格 tracking/offset 不同、match=0，独立期望为 `(M-offset)/tracking`。该文件是显式软件诊断夹具，不能冒充 OSM/THRU 物理标准求得的 TOSM 或自动用于硬件。

成功后用 `SENS1:CORR:CSET:ACT 'SIMULATION_ONLY_FOUR_PORT_DIAGNOSTIC',0;*OPC?` 激活，检查 `SYST:ERR?` 和 `SENS1:CORR?`。完整 16 参数使用 `CALC:DATA:MSD? '1,2,...,16'`，配合 `FORM REAL,64;:FORM:BORD SWAP`。序号按测量定义顺序，调用者需先建立对应的 S11～S44。

校准集冻结频轴、点数、IFBW、逐点功率和身份。改变刺激时应重新生成明确的软件夹具或导入匹配的实际校准文件，不能改变轴后继续套用旧集。`*RST` 保留池；默认 `server.calibrationStore: calibration` 使 Ch1 池和选择跨重启恢复，校正仍需显式激活。关闭持久化时需 Export/Import 或重新生成软件夹具。每通道最多 256 集、512 MiB，仪器合计 1 GiB；其他逻辑通道当前不自动持久化。不同路径可有不同实际系数和名称，不允许用多份名字冒充物理校准精度。

## 本轮验证

- 全量构建及类型检查通过；91 个后端 CTest 均通过，包含首次候选崩溃残留清理、保存失败、Windows manifest 替换失败、陈旧写入冲突、损坏启动拒绝及 OPC 等保存屏障。
- 前端 546 项通过；真实隔离服务浏览器验证四口 18/24 步、十组端口/端口对、最后 P3–P4 可达及未采集时 Apply 禁用。204 集列表使用六行原生滚动选择，浏览选项不会自动启用校正。
- `calibration_math` 对原 `040D80` 四口黄金向量校对，并用反例区分独立二口拼接；`service_four_port_calibration` 用独立前向模型生成 18/24 个标准和另一个稠密 DUT，核对误差表及全部 16 项。
- `vnb_scpi_integration` 验证实际 TCP、四口非单位系数、16 条同帧 REAL64、大小端、IEEE 块、10001 点传输预算、过期/部分覆盖拒绝及复位保留池。
- 独立 40 GHz 模拟进程通过上述脚本核对 16×800 个复数点，记录最大误差为 0；这只验证指定 match=0 诊断模型的软件数值链。
- 最新 12:29:49 二进制在独立实例验证 10 MHz～40 GHz、800 点的 16×800 校正及正式导出，最大误差 0；硬重启后 1 集 ID、选择及完整导出相同，校正关闭。
- `calibration_persistence_integration` 实际导入 204 个不同名、800 点、独立非单位四口系数集，核对首尾同帧 16S、ID/名称/完整系数、`*RST`、两次同目录硬重启及持久删除；整体 195.83 秒，两个大池恢复分别 44.73/44.19 秒。快速导入实际触发操作记录保护，保持租约并用相同请求重试 4 次后完成，未扩大资源预算。

首次大池运行曾出现 HTTP 5 秒连接超时，提交前审查在并行和串行运行中再次复现。现场有大量 TIME_WAIT，测试每 5 毫秒创建短连接轮询进度；改为 50 毫秒后，保持原 5 秒请求超时、120 秒操作期限及全部断言通过。短连接积压是已观察的资源压力，尚未证明具体 OS 分配失败点。另一次恢复超出原 45 秒启动等待，经独立 205 集副本确认是正常解析耗时；优化前后为 84.16/53.14 秒、CPU 为 76.33/49.33 秒。验收为启动保留 120 秒余量，不将这些 Debug 实测时间宣称为其他机器或最大池的启动上限。

原程序理想及非平凡误差的机械组件黄金链均已补回；后者使用非理想 OSL/THRU，当前服务仍仅开放理想标准，详见[实现依据](research/四端口TOSM实现依据.md)。数值核、组件 oracle 和产品独立回归分开记录；软件夹具不证明真实硬件精度或校准溯源。

## CableTest 10 MHz 全链验收：已完成

2026-10-16，CableTest 对话使用 12:29:49 构建完成两轮实际 TCP/HTTP 联调。已核对其完成记录、204 项逐集导出核对结果及当前发布 EXE 的完整 SHA256：`1b208c33a01b750c0c569bdb583bf0bf6be72ab80d2446bccbb7dd6552f9c6b1`。频轴为 10 MHz～40 GHz、原始 800 点、分析 4000 点，IFBW 1 kHz、0 dBm。

- 首轮 5 项 live 验收通过，耗时 267.41 秒，覆盖冒烟、完整 SI 108 批、完整 TDR 96 批及缺失校准集负例。SI 有 32 个分组、64 项分组指标；TDR 有 192 个逻辑结果。
- 硬停止进程后，同目录恢复 205 集耗时 41.372 秒；204 路名称对应的 ID 与完整导出系数全部相等，没有重新导入，205 个系数文件的大小和修改时间均未改变。
- 重启后完整 SI 108 批及 TDR 96 批复跑通过，共 2 项、255.53 秒，各类别计数与首轮一致，无剩余复跑项。
- CableTest 另完成 756 项回归，跳过 5 项需显式启用的 live 测试；后续 TDR 别名修改的指标及 HTTP 前置检查共 105 项通过。这些是 CableTest 测试计数，与上述 VNB 91 项后端测试分别记录。

验收[汇总记录](../reference/analysis/vnb-software-e2e-20261016/cabletest-10mhz-20261016-125204/integration-verification-summary.json)和[重启记录](../reference/analysis/vnb-software-e2e-20261016/cabletest-10mhz-20261016-125204/restart-verification.json)已从原临时目录保存到本项目 reference，副本 SHA256 与原文件一致；记录含两轮 run ID 和完整产物路径。该隔离验收进程已停止，存储与产物保留。

旧 12:13:37 构建、50 MHz 起扫的结果仅保留为链路及持久化记录，不能仅补 DC 后满足 10 MHz 分析网格，不作为 TDR 数值有效性依据。上述新版验收仍是 NativeSimulator 加模拟矩阵的软件联调，不代表实体路径完成校准或实机 RF 精度通过验证。

## 提交前审查

审查修复了已选 Single 时 `INIT:CONT OFF` 漏停、`*RST` 未恢复默认全局显示、普通通道复位新建图窗误改全局布局，以及目录枚举/Python 资源释放问题。补充回归保留完整帧停止与 OPC 屏障、其他通道帧及显示不受单通道复位影响的断言。

审查后 Debug EXE 的 SHA256 为 `f1a94b10ea2184c99e1a7e2467282e7378d2f5f336b837cb8848da68536b8ad4`；在隔离临时目录完成 204 集验收，耗时 199.16 秒，导入 98.25 秒，两次恢复 43.73/44.00 秒。此版本未重复 CableTest 全链验收；上节的 CableTest 结果仍对应其记录的旧哈希。用户部署的 `release` 配置、校准池和 EXE 未被本次审查构建覆盖。审查范围、规范覆盖限制及验证见[审查记录](research/四端口TOSM与持久化审查.md)。

## 校准池启动与切换性能复测

2026-10-16，在真实 205 集池的独立副本中测量，原 JSON 共 167,618,759 字节，逻辑池为 122.68 MiB。以下为同机 MinGW 10.3 Debug、文件缓存未清空的原生调用结果，不是冷盘上限。优化只调整 Windows JSON 数字转换及关键词分派，没有延迟加载、修改存储格式或重启用户实例。

| 阶段 | 优化前 | 优化后 |
| --- | ---: | ---: |
| 205 × 800 点读取、摘要与解析 | 40.429 s | 7.367 s |
| 全池系数校验及工作区冻结 | 1.080 s | 1.093 s |
| 单集 800 点读取解析 | 198.9 ms | 37.7 ms |
| 单集 4000 点读取解析 | 1006.2 ms | 191.4 ms |
| 单集 6700 点读取解析 | 1650.2 ms | 318.9 ms |
| 205 集池热选择持久化 | 约 5 ms | 中位 5.71 ms |

高点数单集是保持四口真实数组形状的显式性能夹具，不代表校准精度。首次恢复后的工作区与存储指针不同，第一次选择保存仍约 101 ms；随后复用未改变的系数文件。修改一个集约 100 ms，强制重写全部 205 集约 17.7 s，这些成本不能混称为每次激活耗时。原九个热点源单独开启 `-O2`、仍用原数值转换时读取解析仍约 35.8 s，说明只更换优化等级不足以消除主瓶颈。

新增独立进程基准可在当前服务运行期间测副本：

先按本地工具链在 `cmake-build-calibration-perf` 独立构建 `vna_server`，并将 `VNAPRO_RELEASE_DIR` 指向该目录下的 `bundle`，再执行（路径以实际构建产物为准）：

```powershell
python -B -X utf8 VnaServer/tests/CalibrationStartupBenchmark.py --server cmake-build-calibration-perf/bin/vna_server.exe --driver cmake-build-calibration-perf/bin/libtdc1vn10_drv_sim.dll --store release/calibration --config config/vnb-software-40ghz.yml --repeat 2 --max-seconds 20 --switch-all
```

脚本只读取源 manifest 及其引用文件，复制期间 manifest 改变则拒绝；使用临时程序、配置、日志及存储，随机监听端口，退出只停止自身子进程。输出源 manifest SHA256、从进程创建至首个 HTTP 状态验证的耗时；可选 `--switch-all` 仅遍历当前刺激兼容的名称，逐次等待 ACT、`*OPC?` 落盘屏障及错误查询，单独报告首项、中位、p95 和整轮耗时。`--max-seconds` 是本机自选回归门槛，不是产品保证；点数、设备或刺激不匹配的集明确计入跳过数。此工具测启动和选择，不包含硬件切换、扫描、采集或 CableTest 指标分析。

相同 205 集源 manifest（SHA256 `429579eac902f1dc3b1a336a05e609010021e8997154811f69bccbd926224545`）的完整优化进程实测：从创建进程至 HTTP 状态校验分别为 14.61/12.86 s；逐名激活、OPC 保存完成及错误查询分别为 7.172/7.015 s，中位均为 31 ms，p95 为 62/47 ms，首次约 219/203 ms。旧版同工具完整测得启动 76.05 s、逐名激活 9.640 s、中位 47 ms、p95 63 ms、首次 140 ms；旧版触发 20 s 本机启动门槛，优化版两次通过。205 集全部兼容、无跳过；整轮选择约减少 2.5 s，首次选择的单次样本并未变快，不能承诺每项都更快。该整轮不包含扫描及指标分析，也不等同于 CableTest 全链复跑。

这组端到端结果应与上表原生计时区分。原版在相同机器独立进程另测到约 43 s 的启动时间，存在明显差异；因此既不承诺固定倍率，也不把纯服务微秒/毫秒值当作 TCP 延迟。Windows `_strtod_l` 仍需构造完整 JSON 值树；当前实测完整启动还有约 13～15 s。若要求继续缩短，应再单独评估带源摘要校验的紧凑派生缓存，并处理源数据、缓存和双份驻留的预算；本轮未新增第二种持久化格式。

JSON 回归保留溢出/下溢、非正规数、负零、复用失败恢复及逗号地区设置；同工具链另外进行了 240,032 次新旧解析差分，覆盖随机 binary64 文本、长十进制、四种舍入模式及真实 C 逗号 locale，接受结果与位型均相同。平台 API 的地区设置合同见 [Microsoft 文档](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strtod-strtod-l-wcstod-wcstod-l?view=msvc-170)，极端值回退仍以既有流的实际行为为准。后续容量及插值边界见[校准接入与使用](校准接入与使用.md)。

本轮 13 项相关 CTest 通过，包含数值核、完整四口标准、校准池及激活、Web/SCPI 协议、日志和保存屏障。完整 204 集持久化脚本两次通过：Python 3.12.14 直接运行 169.50 s（导入 121.20 s、恢复 16.25/16.59 s）；Python 3.12.9 在原 CTest 工作目录、增加只读现场监视的运行 172.95 s（导入 125.55 s、恢复 16.06/16.22 s）。二者均验证完整系数/ID、同帧 16S、复位、持久删除、真实保存失败回滚及坏池拒绝。

验收限制：未加监视的原 `calibration_persistence_integration` CTest 两次在约第 40/43 次导入后，GET operation 的 `socket.connect` 发生 5 s 超时，服务进程仍存活；请求尚未发送。HTTP 请求超时仍为 5 s，轮询仍为 50 ms，没有为通过而增加重试。TIME_WAIT 数量未接近动态端口总量，不能认定端口耗尽；后续 45 s 采样未捕获持续阻塞。另一次相同解释器、CTest 入口及工作目录的带监视 64 集窄化诊断通过（61.40 s），不能冒充 204 集验收，也不能证明根因已修复。本轮不将原始 CTest 入口宣称为全绿，不据单次通过归因 Python 版本，不修改网络实现；已有启动、TCP 切换和完整数据验收结果与该未定位现象分别记录。
