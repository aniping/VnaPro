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
