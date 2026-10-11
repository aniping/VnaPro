# 服务端日志

日志使用 **spdlog 1.17.0** 的静态库和随附 fmt，适配 C++14。
选型依据与企业公开采用证据见 [C++ 日志库选型](../docs/research/C++日志库选型.md)。
`VnaLog` 统一配置和生命周期；格式化、线程安全输出、文件滚动由开源库完成。

## 默认行为

服务启动前加载日志配置，再创建采集 worker；正常结束时先关闭设备和 worker，再刷新、关闭日志。
默认输出至终端 stderr 和构建目录 `bin/logs/vnapro.log`，内容为 UTF-8。
示例格式：

```text
2026-10-11 16:30:42.123 [INFO] [1234] VnaCore - 采集已启动，run=1，revision=2，mode=CONTINUOUS
```

记录初始化、配置提交、运行/停止、故障、监听、控制连接和退出。
INFO 默认不打印逐帧数据；TRACE 才记录完整帧的 run、frame、revision、点数和端口数。
HTTP 方法、已解析路径和响应状态在 DEBUG 输出，不记录控制令牌、请求正文或 IQ 数组。
监听地址通过 INFO 日志报告，客户端应从日志等待 SCPI/Web 监听就绪。

默认每个活动文件最大 **10 MiB**，保留 **5 个备份**，单条完整输出最多 **64 KiB**。
新目录按该配置最多有活动文件和 5 个归档，即约 60 MiB；备份数不包含活动文件。
文件名为 `vnapro.log`、`vnapro.1.log` … `vnapro.5.log`，编号越大越旧。
超过文件大小前先滚动，淘汰当前策略内最旧的备份。

## 配置

默认配置源为 [config/vnapro.yml](../config/vnapro.yml)。
CMake 配置时复制到构建目录 `bin/vnapro.yml`，默认启动读取该副本。
编辑源配置后重新执行 CMake 配置；需要保留独立运行配置时，显式指定文件：

```powershell
.\build\bin\vna_server.exe --config I:\vnapro-config\vnapro.yml
```

迁移可执行程序时携带配置，并用 `--config` 指向部署文件；默认路径是构建时生成的绝对路径。
配置变更在服务重启后生效，不提供运行中热更新。
文件路径相对**配置文件所在目录**解析，Windows 支持中文配置目录、配置文件名和日志路径。
设备库和 Web 根目录仍遵循既有启动参数合同。

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

统一使用 UTF-8 YAML，全部日志项置于 `log` 映射下，示例见默认配置。支持空行、`#` 注释与可选 UTF-8 BOM；只接受一个文档、已登记的键及严格类型，拒绝 null、未知键、重复键与溢出。布尔值为不加引号的 `true` / `false`，大小和个数为不加引号的无符号十进制整数。配置文件最多 64 KiB。旧 properties 和 `--log-config` 已移除，部署时改用 `--config vnapro.yml`。
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
采用同步写入；默认 INFO 事件较少，避免引入额外队列和丢日志策略。
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

首次 CMake 配置下载固定版本的官方源码，并校验 SHA256，第三方许可随原始源码保留。
离线环境可以显式指定**已解包的 1.17.0 源码目录**：

```powershell
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SPDLOG=I:/dependencies/spdlog-1.17.0
```

统一配置还依赖 yaml-cpp 0.9.0；完全离线构建时同时指定其已解包源码目录：

```powershell
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SPDLOG=I:/dependencies/spdlog-1.17.0 -DFETCHCONTENT_SOURCE_DIR_YAML_CPP=I:/dependencies/yaml-cpp-0.9.0
```

显式源码覆盖不会下载或验证归档摘要，调用者应确认其版本；默认构建不依赖被忽略的 `third-part` 目录。
测试覆盖格式与过滤、备份淘汰、零备份、UTF-8 截断、长记录硬上限、启动轮转、
关闭刷新、旧缓冲重配、800 条多模块并发记录，以及实际服务的配置错误、驱动失败、
采集摘要和 Windows 中文配置/日志路径。

2026-10-11 验证：Windows MinGW/GCC 10.3 全量 16 项 CTest 通过（16.32 秒）；
WSL/Linux GCC 13.3 全目标 ThreadSanitizer 16 项通过（11.71 秒），未报告数据竞争。
两个环境均覆盖原有 SCPI、Web、golden/noise 采集链路；macOS 未在本轮验证。
最后补充参数编码错误处理后，两个环境的日志相关 3 项测试复验通过。
