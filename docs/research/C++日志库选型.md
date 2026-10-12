# C++ 日志库选型调研

调研日期：2026-10-11。

## 结论与适用范围

本项目优先采用 **spdlog v1.17.0**，固定版本、提交和源码包校验值。
它支持现有 C++14 工具链，提供参数化消息、可配置格式、级别、多输出目标、按大小滚动和可选异步处理。
这项选择依据项目需求、官方源码和公开工程实践，不以 GitHub 星数或未经验证的性能排名作为决定依据。
[spdlog 官方说明](https://github.com/gabime/spdlog/blob/v1.17.0/README.md)。

“大厂常用”应理解为可核查的项目采用证据。
某个公司开源项目使用某库，不能证明整个公司统一使用该库，也不能直接证明它最适合本项目。
以下明确区分公开采用事实与本项目的选择判断。

用户所说的 SLF4J 风格，对应统一调用入口、`{}` 参数化消息、时间/线程/级别/模块格式及后端配置。
SLF4J 本身是 Java 日志门面，文件轮转由具体后端完成，并没有规定唯一的日志行格式。
本项目可以实现相似的使用体验；无需实现 Java API、Logback XML 或动态更换多个日志后端。
[SLF4J 官方手册](https://slf4j.org/manual.html)。

## 可以核查的企业项目实践

| 项目 | 已核查事实 | 证据范围 |
| --- | --- | --- |
| Microsoft PowerToys | 官方开发文档说明 C++ 项目使用 spdlog；公共 Logger 源码包含 spdlog 类型和 sink | 证明 PowerToys 的采用，不推定微软全部项目的标准 |
| Google glog | 官方仓库提供 Google 日志库；2025-06-30 归档并明确不再维护 | 证明其来源和历史，不能把历史使用当作当前维护承诺 |
| Google Abseil | 官方说明代码来自 Google 自身 C++ 代码库并用于生产；Logging 属于该库 | 是持续维护方向，但当前版本要求 C++17 |
| 百度飞桨 Paddle | `develop` 构建脚本配置 `GLOG_TAG v0.4.0`、创建 glog 导入目标并链接 | 证明飞桨公开工程仍使用 glog，不等于新项目应选择已归档上游 |

PowerToys 的直接证据：
[开发文档](https://github.com/microsoft/PowerToys/blob/main/doc/devdocs/logging.md)、
[公共 Logger 头文件](https://github.com/microsoft/PowerToys/blob/main/src/common/logger/logger.h)、
[Logger 实现](https://github.com/microsoft/PowerToys/blob/main/src/common/logger/logger.cpp)。
实现可核查 `daily_file_sink_mt`、可配置级别和时间/进程/线程格式。

Google 的直接证据：
[glog 仓库归档及弃用声明](https://github.com/google/glog)、
[Abseil 稳定版说明](https://github.com/abseil/abseil-cpp/blob/20260817.0/README.md)、
[Abseil Logging 官方指南](https://abseil.io/docs/cpp/guides/logging)。
glog 官方建议考虑 Google 维护的 Abseil Logging 或社区维护的 ng-log；本次不额外引入后者。

飞桨的直接证据：
[glog 构建脚本](https://github.com/PaddlePaddle/Paddle/blob/develop/cmake/external/glog.cmake)。
引用的默认开发分支可能变化；这里记录调研当日看到的采用事实，不把它作为本项目依赖版本。

## 候选库比较

稳定版本通过官方发布页核查；语言要求检查对应标签的源码，避免误用开发分支要求。
下表的“成本”和“适配判断”是结合本项目 C++14、Windows/Linux、CMake 和轻量架构作出的判断。

| 库/核查版本 | C++14 与平台 | 格式和级别 | 文件滚动与异步 | 许可/引入成本 | 本项目判断 |
| --- | --- | --- | --- | --- | --- |
| spdlog 1.17.0 | C++11 起；Windows/Linux | `{fmt}` 参数化、pattern、运行时级别 | 大小和每日 sink；可选有界异步队列 | MIT；可使用随库附带的 fmt，支持静态库 | 首选，所需能力直接可用 |
| glog 0.7.1 | C++14；Windows/Linux | 流式宏、严重级别、详细日志；风格偏 Google | 文件大小和清理配置；不采用 spdlog 式异步 sink 模型 | BSD-3-Clause；可选 gflags | 上游已归档，不作为新依赖 |
| Abseil 20260817.0 | 当前最低 C++17；跨平台 | `LOG/CHECK`、Google 前缀、可扩展 sink | 可扩展输出；需额外实现本项目期望的滚动/格式组合 | Apache-2.0；引入 Abseil 依赖图 | 因语言版本和整合成本排除 |
| log4cplus 2.2.1 | 稳定标签要求 C++11；Windows/Linux | 接近 Log4j；PatternLayout、属性配置、MDC/NDC | RollingFile、Daily/TimeBased、AsyncAppender | 源码按文件使用 BSD-2-Clause 或 Apache-2.0，线程池另有许可 | 可用备选；需要 Java 式配置体系时有价值 |
| Boost.Log 1.92.0 | 日志库最低 C++11；Windows/Linux | 可组合属性、过滤器和格式器 | 文件大小/时间轮转、归档收集器、异步 frontend | Boost Software License 1.0；多个 Boost 组件 | 功能完整，当前项目引入成本较高 |
| Quill 13.0.0 | 最低 C++17；Windows/Linux 等 | `{fmt}`、pattern、级别和上下文 | 后端异步处理；大小/时间轮转 | MIT；随库附带 fmt | 适合延迟敏感场景，当前 C++14 排除 |

spdlog 的语言与构建选项依据：
[稳定标签 CMake](https://github.com/gabime/spdlog/blob/v1.17.0/CMakeLists.txt)、
[稳定版 README](https://github.com/gabime/spdlog/blob/v1.17.0/README.md)、
[MIT 许可](https://github.com/gabime/spdlog/blob/v1.17.0/LICENSE)。
随库 fmt 12.1.0 的升级记录见
[1.17.0 发布说明](https://github.com/gabime/spdlog/releases/tag/v1.17.0)。

glog 的版本、配置和许可依据：
[0.7.1 发布页](https://github.com/google/glog/releases/tag/v0.7.1)、
[官方文档](https://google.github.io/glog/stable/)、
[源码许可](https://github.com/google/glog/blob/v0.7.1/COPYING)。
停止维护状态以官方仓库公告为准，不能仅凭最后一次发布页判断。

Abseil 的语言要求、日志接口和许可依据：
[20260817.0 发布页](https://github.com/abseil/abseil-cpp/releases/tag/20260817.0)、
[对应 README](https://github.com/abseil/abseil-cpp/blob/20260817.0/README.md)、
[Logging 指南](https://abseil.io/docs/cpp/guides/logging)、
[Apache-2.0 许可](https://github.com/abseil/abseil-cpp/blob/20260817.0/LICENSE)。

log4cplus 的当前稳定版发布于 2026-10-06；稳定版 CMake 设置 C++11。
开发分支 README 已写 C++23，不能据此误判 2.2.1 不兼容本项目。
[2.2.1 发布页](https://github.com/log4cplus/log4cplus/releases/tag/REL_2_2_1)、
[稳定版 README](https://github.com/log4cplus/log4cplus/blob/REL_2_2_1/README.md)、
[稳定版 CMake](https://github.com/log4cplus/log4cplus/blob/REL_2_2_1/CMakeLists.txt)。
滚动、异步及许可依据：
[FileAppender](https://github.com/log4cplus/log4cplus/blob/REL_2_2_1/include/log4cplus/fileappender.h)、
[AsyncAppender](https://github.com/log4cplus/log4cplus/blob/REL_2_2_1/include/log4cplus/asyncappender.h)、
[许可文件](https://github.com/log4cplus/log4cplus/blob/REL_2_2_1/LICENSE)。

Boost.Log 的能力和成本依据：
[平台/语言要求](https://www.boost.org/doc/libs/1_92_0/libs/log/doc/html/log/installation.html)、
[库及依赖列表](https://www.boost.org/library/latest/log/)、
[文件后端](https://www.boost.org/doc/libs/1_92_0/libs/log/doc/html/log/detailed/sink_backends.html)、
[异步前端](https://www.boost.org/doc/libs/1_92_0/libs/log/doc/html/log/detailed/sink_frontends.html)。
归档收集器的 `max_size` 限制已归档文件总量，不能直接理解为包含正在写入文件的全目录硬上限。

Quill 的语言、平台、异步设计和许可依据：
[13.0.0 发布页](https://github.com/odygrd/quill/releases/tag/v13.0.0)、
[稳定版 README](https://github.com/odygrd/quill/blob/v13.0.0/README.md)、
[CMake 的 C++17 检查](https://github.com/odygrd/quill/blob/v13.0.0/CMakeLists.txt)、
[滚动文件类型](https://github.com/odygrd/quill/blob/v13.0.0/include/quill/sinks/RotatingFileSink.h)。
未使用 Quill 自己给出的性能排名证明它在本网分采集场景优于其他库；这种结论需要本项目实测。

## spdlog 的可复现版本

| 项目 | 固定值 |
| --- | --- |
| 稳定发布 | `v1.17.0`，发布于 2026-01-04 |
| 对应提交 | `79524ddd08a4ec981b7fea76afd08ee05f83755d` |
| 官方源码包 | `https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.17.0` |
| 源码包大小 | `281801` 字节 |
| 源码包 SHA256 | `d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744` |

发布/提交通过
[官方 release](https://github.com/gabime/spdlog/releases/tag/v1.17.0) 和
[官方标签 API](https://api.github.com/repos/gabime/spdlog/git/ref/tags/v1.17.0) 核查。
源码包大小与 SHA256 是调研时下载官方包后计算的值，并非声称发布者另行签署了该摘要。
默认使用 bundled fmt；C++14 下不启用要求 C++20 的 `SPDLOG_USE_STD_FORMAT`。

2026-10-12 调整依赖交付方式：上述官方源码包以 `third-part/spdlog-1.17.0.tar.gz` 纳入 Git，YAML 解析库同时保存为 `third-part/yaml-cpp-0.9.0.tar.gz`。构建不使用 FetchContent 或外部源码覆盖，CMake 校验本地包后自动解压为 `third-part/spdlog-1.17.0` 和 `third-part/yaml-cpp-0.9.0`，直接引用源码编译；缺包或校验失败直接终止。原始源码和许可保留在包内，生成的解压目录不提交。版本、摘要及升级说明见[第三方库清单](../../third-part/README.md)。此调整只涉及 C++ 库，WebUI 的 npm 依赖不在本次离线交付范围内。

## 实施时必须明确的边界

1. **日志行格式。** `pattern` 控制元数据布局，`{}` 控制消息参数；两者不能混为一个配置。
   时间、毫秒、级别、线程、模块、消息可用内置 formatter 组合。
   [格式说明](https://github.com/gabime/spdlog/wiki/Custom-formatting)。
2. **单文件大小。** `rotating_file_sink` 在预计写入后超过阈值时先滚动。
   单条格式化结果超过阈值时，空文件仍会写入整条消息，因此它本身不保证严格字节硬上限。
   如果产品承诺硬上限，还要限制格式化后的单条输出长度，并处理启动时已经超限的文件。
   [1.17.0 轮转源码](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/sinks/rotating_file_sink-inl.h)。
3. **备份与总量。** `max_files` 表示备份数，不包含活动文件。
   大小轮转和每日轮转是不同 sink；把两者同时挂上只是写两份日志，并不会自动得到“时间或大小任一触发”的单一轮转策略。
   [大小 sink](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/sinks/rotating_file_sink.h)、
   [每日 sink](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/sinks/daily_file_sink.h)。
4. **数据竞争。** 多线程调用使用 `_mt` sink；同一物理日志文件共享唯一 sink。
   不在业务线程运行期间并发修改 `sinks()` 容器或错误处理回调。
   `_mt` 不是跨进程文件锁，多个服务实例应配置独立日志路径。
   [线程安全说明](https://github.com/gabime/spdlog/wiki/Thread-Safety)。
5. **异步队列。** 官方提供队列满时阻塞或丢弃的策略；选择阻塞就可能延迟采集，选择丢弃就应明确计数和可见性。
   一个后台线程可保持队列消费顺序，多后台线程可能重排。
   [异步说明](https://github.com/gabime/spdlog/wiki/Asynchronous-logging)。
6. **退出与持久化。** 1.17.0 的异步 `flush()` 会投递任务，不等待写出。
   停止生产日志的业务线程，再关闭并等待日志线程池；stdio flush 不等于磁盘掉电持久化。
   [异步 logger 源码](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/async_logger-inl.h)、
   [线程池源码](https://github.com/gabime/spdlog/blob/v1.17.0/include/spdlog/details/thread_pool-inl.h)。
7. **DLL 边界。** header-only 日志注册表不能假定在主程序和 DLL 间共享。
   本项目设备 DLL 继续保持现有 C ABI，优先在核心/服务侧记录设备调用结果，避免跨 ABI 传递 C++ logger 对象。
   [官方 DLL 说明](https://github.com/gabime/spdlog/wiki/How-to-use-spdlog-in-DLLs)。

## 本项目建议的落点与验证

以下是结合项目架构的实施建议，不是声称全部由第三方库自动保证：

- 使用一个小型日志模块统一初始化、配置和生命周期；日志库负责格式化、线程安全 sink 和滚动，不自行重写这些机制。
- 日志配置覆盖级别、格式、控制台开关、文件路径、单文件轮转阈值、备份数量、刷新和异步策略。
- 启动失败、驱动返回码、配置版本、运行编号、状态转换和正常退出有明确日志。
- 连续采集逐帧日志放在 DEBUG/TRACE，记录帧身份和摘要，不在 INFO 输出全部 IQ 数组。
- 网络请求只记录方法、路径和结果，控制令牌及用户原始载荷不进入日志。
- 对配置错误、级别过滤、大小轮转、备份淘汰、长消息、多线程写入、退出排空和不可写路径做验证。
- 日志文件失败不能让日志错误处理递归调用自身；应保留可见的终端错误，并明确服务是拒绝启动还是降级继续。
- 上述配置和故障行为必须写入使用说明；最终实现的具体默认值以对应模块文档为准。
