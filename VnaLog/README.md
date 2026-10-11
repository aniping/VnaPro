# C++日志模块

日志后端使用 spdlog 1.17.0 静态库和随附 fmt，兼容项目 C++14。
选型与公开采用证据见 [C++日志库选型](../docs/research/C++日志库选型.md)。

## 调用和生命周期

程序在生产日志的线程启动前调用 Logger::Instance().Configure(LogConfig)。
线程结束后先 Shutdown，刷新并关闭日志；配置前静默，不自动创建文件。
Configure 只允许在没有并发日志调用时串行执行，失败保留原配置。

```cpp
#include "vna_log/logger.h"

VnaLog::LogConfig config;
VnaLog::Logger::Instance().Configure(config);
VnaLog::Info(VnaLog::Module::CORE, "配置已提交，revision={}", revision);
```

调用入口为 noexcept，在格式化前过滤等级，运行中的输出异常不传播到业务逻辑。
默认行格式为毫秒时间、大写级别、线程、模块和消息；%K 表示大写级别。
各模块共享同一组线程安全 sink，默认同步输出到 stderr 和 logs/vnapro.log。
配置可以控制级别、pattern、文件和终端开关、路径、大小、备份、单条上限和刷新。

默认单文件10 MiB、5个备份（不含活动文件）、单条64 KiB、INFO和自动刷新INFO。
整行按 min(maxRecordBytes,maxFileBytes) 截断，保留UTF-8边界和截断标记。
Windows 文件路径采用UTF-8并转换宽路径；其他平台直接使用UTF-8。
已有超限文件会原样归档，不追溯裁切历史；降低备份数量不删除旧的更大编号。
_mt 只协调进程内线程，多进程使用不同日志路径；刷新不等于掉电持久化。

## 构建

首次 CMake 配置下载固定版本官方源码，并校验 SHA256；保留第三方原始许可。
离线环境可显式指定已解包的1.17.0源码目录：

```powershell
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_SPDLOG=I:/dependencies/spdlog-1.17.0
```

显式覆盖不执行归档下载/校验，调用者需确认版本；默认不依赖忽略的third-part目录。
