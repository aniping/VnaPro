/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_log/logger.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

#include "bounded_pattern_formatter.h"

namespace {

constexpr std::size_t MODULE_COUNT = static_cast<std::size_t>(VnaLog::Module::COUNT);
using Loggers = std::array<std::shared_ptr<spdlog::logger>, MODULE_COUNT>;

bool ValidLevel(VnaLog::Level level)
{
    return level >= VnaLog::Level::TRACE && level <= VnaLog::Level::OFF;
}

spdlog::level::level_enum BackendLevel(VnaLog::Level level)
{
    return static_cast<spdlog::level::level_enum>(level);
}

spdlog::filename_t BackendFilePath(const std::string &path)
{
#ifdef _WIN32
    if (path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Log file_path is too long");
    }
    const auto byteCount = static_cast<int>(path.size());
    const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), byteCount, nullptr, 0);
    if (wideCount <= 0) {
        throw std::invalid_argument("Log file_path must be valid UTF-8");
    }
    std::wstring widePath(static_cast<std::size_t>(wideCount), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), byteCount, &widePath[0], wideCount) !=
        wideCount) {
        throw std::invalid_argument("Log file_path UTF-8 conversion failed");
    }
    return widePath;
#else
    return path;
#endif
}

std::vector<spdlog::sink_ptr> CreateSinks(const VnaLog::LogConfig &config)
{
    std::vector<spdlog::sink_ptr> sinks;
    if (config.fileEnabled) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            BackendFilePath(config.filePath), config.maxFileBytes, config.maxFiles, config.rotateOnOpen));
    }
    if (config.consoleEnabled) {
        auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        console->set_formatter(std::make_unique<VnaLog::Detail::BoundedPatternFormatter>(
            "%^" + config.pattern + "%$", std::min(config.maxRecordBytes, config.maxFileBytes)));
        sinks.push_back(std::move(console));
    }
    const auto recordLimit = std::min(config.maxRecordBytes, config.maxFileBytes);
    if (config.fileEnabled) {
        sinks.front()->set_formatter(
            std::make_unique<VnaLog::Detail::BoundedPatternFormatter>(config.pattern, recordLimit));
    }
    return sinks;
}

Loggers CreateLoggers(const VnaLog::LogConfig &config)
{
    const std::array<std::string, MODULE_COUNT> names{{"VnaCore", "VnaService", "VnaServer", "VnaScpi", "VnaWeb"}};
    const auto sinks = CreateSinks(config);
    Loggers loggers;
    for (std::size_t index = 0U; index < loggers.size(); ++index) {
        auto logger = std::make_shared<spdlog::logger>(names[index], sinks.begin(), sinks.end());
        logger->set_level(BackendLevel(config.level));
        logger->flush_on(BackendLevel(config.flushLevel));
        logger->set_error_handler([](const std::string &) {
            VnaLog::Logger::ReportFailure();
        });
        loggers[index] = std::move(logger);
    }
    return loggers;
}

std::string EscapeMessage(const std::string &message)
{
    constexpr char HEX_DIGITS[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(message.size());
    for (const unsigned char character : message) {
        switch (character) {
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (character < 0x20U || character == 0x7FU) {
                    escaped += "\\x";
                    escaped += HEX_DIGITS[character >> 4U];
                    escaped += HEX_DIGITS[character & 0x0FU];
                } else {
                    escaped += static_cast<char>(character);
                }
                break;
        }
    }
    return escaped;
}

void FlushLoggers(const Loggers &loggers)
{
    for (const auto &logger : loggers) {
        if (logger != nullptr) {
            logger->flush();
        }
    }
}

}  // 匿名命名空间

namespace VnaLog {

Logger &Logger::Instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    Shutdown();
}

void Logger::Configure(const LogConfig &config)
{
    try {
        ValidateConfig(config);
        // 重配前先刷新旧目标，避免新目标以旧缓冲落盘前的尺寸建立轮转计数。
        FlushLoggers(SnapshotAll());
        auto candidate = CreateLoggers(config);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loggers_.swap(candidate);
            level_ = config.fileEnabled || config.consoleEnabled ? config.level : Level::OFF;
        }
        FlushLoggers(candidate);
    } catch (const std::exception &error) {
        throw std::runtime_error(std::string("Logging initialization failed: ") + error.what());
    }
}

Logger::Loggers Logger::SnapshotAll() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return loggers_;
}

std::shared_ptr<spdlog::logger> Logger::Snapshot(Level level, Module module) const
{
    const auto index = static_cast<std::size_t>(module);
    if (!ValidLevel(level) || level == Level::OFF || index >= MODULE_COUNT) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < level_ || level_ == Level::OFF) {
        return nullptr;
    }
    return loggers_[index];
}

void Logger::Shutdown() noexcept
{
    try {
        Loggers previous;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loggers_.swap(previous);
            level_ = Level::OFF;
        }
        FlushLoggers(previous);
    } catch (...) {
        ReportFailure();
    }
}

void Logger::Flush() noexcept
{
    try {
        FlushLoggers(SnapshotAll());
    } catch (...) {
        ReportFailure();
    }
}

bool Logger::Enabled(Level level) const noexcept
{
    try {
        if (!ValidLevel(level) || level == Level::OFF) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return level_ != Level::OFF && level >= level_ && loggers_.front() != nullptr;
    } catch (...) {
        ReportFailure();
        return false;
    }
}

void Logger::Write(Level level, Module module, const std::string &message) noexcept
{
    try {
        const auto logger = Snapshot(level, module);
        if (logger != nullptr) {
            logger->log(BackendLevel(level), "{}", EscapeMessage(message));
        }
    } catch (...) {
        ReportFailure();
    }
}

void Logger::ReportFailure() noexcept
{
    static std::atomic<std::int64_t> lastReport{std::numeric_limits<std::int64_t>::min()};
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto previous = lastReport.load(std::memory_order_relaxed);
    if (previous != now && lastReport.compare_exchange_strong(previous, now, std::memory_order_relaxed)) {
        std::fputs("[VnaLog] log output failed\n", stderr);
    }
}

}  // 命名空间 VnaLog
