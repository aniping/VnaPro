# 服务端日志

日志使用 **spdlog 1.17.0** 的静态库和随附 fmt，适配 C++14。
选型依据与企业公开采用证据见 [C++ 日志库选型](../docs/research/C++日志库选型.md)。
`VnaLog` 统一配置和生命周期；格式化、线程安全输出、文件滚动由开源库完成。

## 默认行为

服务启动前加载日志配置，再创建采集 worker；正常结束时先关闭设备和 worker，再刷新、关闭日志。
默认输出至终端 stderr 和配置文件旁的 `logs/vnapro.log`，内容为 UTF-8；发布包对应项目根目录的 `release/logs/vnapro.log`，开发运行对应构建目录的 `bin/logs/vnapro.log`。
示例格式：

```text
2026-10-11 16:30:42.123 [INFO] [1234] VnaCore - 采集已启动，run=1，revision=2，mode=CONTINUOUS
```

记录初始化、配置提交、运行/停止、故障、监听、控制连接和退出。
INFO 默认不打印逐帧数据；TRACE 才记录完整帧的 run、frame、revision、点数和端口数。
HTTP 方法、已解析路径和响应状态在 DEBUG 输出，不记录控制令牌、请求正文或 IQ 数组。
监听地址通过 INFO 日志报告，客户端应从日志等待 SCPI/Web 监听就绪。

VN10 设备边界使用 `[VN10]` 标记，INFO 记录 DLL 路径、初始化/配置调用、返回码和实际参数摘要；DEBUG 在配置提交前分块输出完整频率/功率/IFBW/原生端口数组，每条至多 16 项。Download 失败时 ERROR 也补完整参数。关闭相应等级时不构造数组文本，成功扫描的 Start/Get 不增加这些记录。采集公司现场日志的配置及字段说明见[真实驱动验收](../docs/TDC1VN10真实驱动流程验收.md#公司现场初始化与下发诊断)。

### SCPI 命令诊断

默认 INFO 记录完整 LF 程序消息的 `event=received`，原文放在 `program`；TCP 分片在组成完整行之前不作为命令记录。每个单元随后记录 `started` 和真实终态 `completed`、`failed` 或 `cancelled`。`received` 只表示收到，`started` 也不代表执行成功。

按 `connection` 关联同一进程内的连接，按 `message` 和从 1 开始的 `unit` 关联分号消息内的具体指令。执行记录含规范 `path`、`query`、实际 `channel`、`code` 和 `stage`。接收原文保留参数及名称；换行和控制字符继续由统一日志层转义。成功的 `code=0`，命令或服务失败使用原错误码并以 WARN 输出，不额外写入或消费仪器错误队列。

异步 INIT、配置、校准保存及多通道复位等待相应 service future 后才记录终态；`*RST` 任一通道失败即记录失败。`*OPC?` / `*WAI` 等待前序操作终结，`priorFailures` 汇总其中失败数，保持原协议行为；`*OPC?` 返回 `+1` 仍不能代替 `SYST:ERR?`。`*CLS` 取消未完成 OPC 事件或断线取消响应编码时记录 `cancelled`，已受理的服务操作仍继续完成并记录结果。

数值查询只记录 `values` 和编码 `bytes`（包含二进制块头、不含消息末尾 LF），不打印 ASCII 数组、REAL64 二进制或 IQ 正文。普通命令的完成记录不是响应正文。传输层记录 `tx_queued`、`tx_complete` 的字节数；后者表示本机发送完成，不证明客户端应用已处理结果。

观察者整行拒绝、消息/单元/字节预算拒绝为 WARN `rejected`；接收/发送失败、发送超时和传输预算错误为 WARN `transport_error`，带 `stage`、原因及可用的平台 `socketError`。接入失败为 WARN `connection_rejected`，尚未创建引擎的连接使用保留编号 `connection=0`，记录连接预算、描述符或套接字原因。连接断开记录 `reason` 和是否仍需排空已受理操作。拒绝发生在分派前时，不把混合消息的合法前缀记作已执行。

诊断旁观记录最多 128 个命令，每个最多 32 个 service future；诊断满额以 WARN `diagnostic_dropped` 明示，仅丢诊断，不拒绝协议命令或改变 OPC/错误队列。命令原文受既有 16 KiB 输入预算和下述日志记录上限约束。关闭相应日志等级时不构造命令诊断正文；频繁发送命令会增加同步日志 I/O，可通过 `log.level` 调整输出阈值。

默认每个活动文件最大 **10 MiB**，保留 **5 个备份**，单条完整输出最多 **64 KiB**。
新目录按该配置最多有活动文件和 5 个归档，即约 60 MiB；备份数不包含活动文件。
文件名为 `vnapro.log`、`vnapro.1.log` … `vnapro.5.log`，编号越大越旧。
超过文件大小前先滚动，淘汰当前策略内最旧的备份。

## 配置

默认配置源为 [config/vnapro.yml](../config/vnapro.yml)。
CMake 配置时生成适配当前平台驱动库名的开发配置 `bin/vnapro.yml`；构建 `vna_server` 后首次发布同步到项目根目录的 `release/vnapro.yml`。
程序固定读取自身目录的 `vnapro.yml`，不接受启动参数。重复发布保留已有配置，最新默认配置更新到 `vnapro.example.yml`，按需合并后重启；开发目录的配置在重新执行 CMake 配置时更新。启动示例：

```powershell
.\release\vna_server.exe
```

部署时复制整个 `release` 目录，配置随程序携带。日志在运行时创建，发布同步保留已有日志和用户文件；步骤见[自动生成发布目录](../README.md#自动生成发布目录)。
配置变更在服务重启后生效，不提供运行中热更新。
日志路径相对**配置文件所在目录**解析，Windows 支持中文程序目录和日志路径。
监听、驱动及模拟噪声也由同一 YAML 配置；驱动库只填写文件名并从程序目录加载，Web 根目录相对程序目录解析，配置项见[网络与驱动配置](../README.md#网络与驱动配置)。

| 键 | 默认值 | 含义 |
| --- | --- | --- |
| `log.level` | `INFO` | 输出阈值：TRACE、DEBUG、INFO、WARN、ERROR、CRITICAL、OFF |
| `log.console.enabled` | `true` | 是否写终端 stderr；终端支持时显示级别颜色 |
| `log.file.enabled` | `true` | 是否写滚动文件 |
| `log.file.path` | `logs/vnapro.log` | UTF-8 文件路径，不允许空值 |
| `log.file.maxSizeBytes` | `10485760` | 单文件大小，1024…4294967296 字节 |
| `log.file.maxFiles` | `5` | 0…1000 个备份；0 表示滚动时只重置活动文件 |
| `log.file.maxRecordBytes` | `65536` | 单条完整输出限制，32…4294967296 字节 |
| `log.file.rotateOnOpen` | `false` | true 时启动即归档已有非空活动文件 |
| `log.flush.level` | `INFO` | 达到该级别后立即刷新；OFF 只手动刷新或退出刷新 |
| `log.pattern` | 见配置文件 | 元数据和消息布局，1…1024 字节，不允许换行 |

格式沿用 spdlog pattern：`%Y-%m-%d %H:%M:%S.%e` 为毫秒时间，`%t` 为线程 ID，
`%n` 为模块，`%v` 为消息，`%K` 是本项目扩展的大写级别。
消息中的 `{}` 是参数占位符，与 pattern 的 `%` 字段不同。
用原生 spdlog 选项调整格式，不能直接填 SLF4J/Logback 的格式语法或 XML。
其他字段见 [spdlog 格式说明](https://github.com/gabime/spdlog/wiki/Custom-formatting)。

统一使用 UTF-8 YAML，全部日志项置于 `log` 映射下，示例见默认配置。支持空行、`#` 注释与可选 UTF-8 BOM；只接受一个文档、已登记的键及严格类型，拒绝 null、未知键、重复键与溢出。布尔值为不加引号的 `true` / `false`，大小和个数为不加引号的无符号十进制整数。配置文件最多 64 KiB。旧 properties 和启动配置参数均已移除，部署时编辑程序旁的 `vnapro.yml`。
配置不存在或日志目标无法打开时，在创建设备前报错并以退出码 2 结束。

## 大小与保留边界

原生 spdlog 阈值无法约束超长单条消息，所以本模块限制**整条格式化结果**，
实际限值为 `min(log.file.maxRecordBytes, log.file.maxSizeBytes)`，包括时间、模块和行尾。
超过后在完整 UTF-8 字符边界截断，追加 `...[truncated]`，文件采用 LF 行尾。
消息中的换行、制表符和控制字符转为可见转义，避免一条记录伪造多行。

限值约束按当前配置新写出的记录及新生成的文件，不追溯裁切已有历史文件。
如果旧活动文件已经超过新限值，下次写入会将它原样归档，新活动文件按新限值写入。
降低备份数量不会自动删除历史较大编号的文件，例如从 5 改为 2 后旧 `.3`…`.5` 仍可能存在。
需要严格限制整个日志目录占用时，将旧文件归档到其他位置或改用空目录。
本配置不包含按天清理、全目录磁盘配额和压缩；不以单文件大小代替这些能力。

## 并发、故障与调用

各模块共享同一组 `_mt` sink，同一个日志文件只打开一个线程安全写入目标。
采用同步写入，不引入额外日志队列；SCPI 命令频繁时会增加同步写入次数。
开启 TRACE 后会增加磁盘写入，适合短期诊断。同步日志不承诺硬实时或磁盘阻塞时限。
`_mt` 只协调进程内线程；多个服务实例必须配置独立日志路径。

Logger 的配置只能在日志生产线程启动前，或全部停止后串行执行。
重新配置前刷新旧缓冲，再创建新目标；配置失败保留原配置。
禁止写入期间重新配置同一路径，以免两个独立 sink 的轮转状态相互干扰。
正常退出刷新不等于掉电持久化；Windows 强制终止进程无法执行正常收尾。

业务代码使用统一入口：

```cpp
#include "vna_log/logger.h"

VnaLog::Info(VnaLog::Module::CORE, "配置已提交，revision={}，points={}", revision, points);
VnaLog::Trace(VnaLog::Module::CORE, "完整帧已发布，run={}，frame={}", runId, frameSeq);
```

等级过滤发生在消息格式化前。调用入口为 noexcept，运行中的格式化或输出异常不向采集、控制逻辑传播。
失败时最多每秒向 stderr 输出一条固定兜底诊断，避免递归调用日志后端。
参数表达式仍由 C++ 调用者先求值，因此不要在调用前拼装大量字符串或完整数组。
设备 DLL 保持现有 C ABI，由核心侧记录设备返回结果。

## 构建与验证

spdlog 1.17.0 和统一配置所需的 yaml-cpp 0.9.0，均以官方 release 源码压缩包纳入仓库的 `third-part` 目录。CMake 配置时校验包的 SHA256，再自动解压为 `third-part/spdlog-1.17.0` 和 `third-part/yaml-cpp-0.9.0` 并直接引用源码；解压产物不纳入 Git。缺包、摘要不符或解压失败直接报错，没有网络下载回退。原始第三方许可保留在压缩包内，包清单、路径与升级流程见[第三方库说明](../third-part/README.md)。

构建使用[根目录构建步骤](../README.md#构建和测试)，离线环境无需手工解压或设置外部源码路径。旧 `FETCHCONTENT_SOURCE_DIR_SPDLOG` 和 `FETCHCONTENT_SOURCE_DIR_YAML_CPP` 参数不再生效；本次仅保证 C++ 依赖来自本地压缩包，WebUI 的 npm 依赖未打包。

测试覆盖格式与过滤、备份淘汰、零备份、UTF-8 截断、长记录硬上限、启动轮转、
关闭刷新、旧缓冲重配、800 条多模块并发记录，以及实际服务的配置错误、驱动失败、
采集摘要和 Windows 中文配置/日志路径。

`service_scpi_logging` 用受控采集和保存 future 验证异步完成、失败、两通道复位、OPC 取消、WAI、断线、数组摘要及 INFO/OFF 协议等价；`scpi_logging_integration` 用独立进程和随机端口核对默认 INFO、TCP 分片、观察者拒绝、错误队列及连接/消息预算日志。两者均不连接正在使用的仪器实例。

2026-10-11 验证：Windows MinGW/GCC 10.3 全量 16 项 CTest 通过（16.32 秒）；
WSL/Linux GCC 13.3 全目标 ThreadSanitizer 16 项通过（11.71 秒），未报告数据竞争。
两个环境均覆盖原有 SCPI、Web、golden/noise 采集链路；macOS 未在本轮验证。
最后补充参数编码错误处理后，两个环境的日志相关 3 项测试复验通过。
