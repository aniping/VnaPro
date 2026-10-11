/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_LOG_LOGGER_H
#define VNAPRO_VNA_LOG_LOGGER_H

#include <spdlog/logger.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "vna_log/log_config.h"

namespace VnaLog {

class Logger final {
public:
    static Logger &Instance();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

    // 工作线程启动前配置；测试可在没有日志调用时串行重配。失败抛出 runtime_error。
    void Configure(const LogConfig &config);
    // 调用方先结束业务线程；已取得的写入快照仍保持有效。
    void Shutdown() noexcept;
    void Flush() noexcept;
    // 未配置或等级关闭时返回 false，不创建默认文件或控制台输出。
    bool Enabled(Level level) const noexcept;
    // 原始文本按单条记录写入，控制字符转为可见转义，不影响采集异常处理。
    void Write(Level level, Module module, const std::string &message) noexcept;
    // 格式化与后端失败共用的无分配兜底，禁止递归进入日志系统。
    static void ReportFailure() noexcept;

private:
    using Loggers = std::array<std::shared_ptr<spdlog::logger>, static_cast<std::size_t>(Module::COUNT)>;

    Logger() = default;
    ~Logger();
    std::shared_ptr<spdlog::logger> Snapshot(Level level, Module module) const;
    Loggers SnapshotAll() const;

    mutable std::mutex mutex_;
    Loggers loggers_;
    Level level_ = Level::OFF;
};

// 只有参数透传依赖模板；格式解析完全委托 spdlog 随附的 fmt。
template <typename... Args>
void Log(Level level, Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    try {
        auto &logger = Logger::Instance();
        if (logger.Enabled(level)) {
            logger.Write(level, module, spdlog::fmt_lib::format(format, std::forward<Args>(args)...));
        }
    } catch (...) {
        Logger::ReportFailure();
    }
}

template <typename... Args>
void Trace(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::TRACE, module, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Debug(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::DEBUG, module, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Info(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::INFO, module, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Warn(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::WARN, module, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Error(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::ERROR_LEVEL, module, format, std::forward<Args>(args)...);
}

template <typename... Args>
void Critical(Module module, spdlog::format_string_t<Args...> format, Args &&...args) noexcept
{
    Log(Level::CRITICAL, module, format, std::forward<Args>(args)...);
}

}  // 命名空间 VnaLog

#endif
