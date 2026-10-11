/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_LOG_LOG_CONFIG_H
#define VNAPRO_VNA_LOG_LOG_CONFIG_H

#include <cstddef>
#include <string>

namespace VnaLog {

// 日志级别同时用于输出筛选和自动刷盘阈值。
enum class Level {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    // 避开 WinSDK 的 ERROR 宏，配置文件中的级别文本仍使用 ERROR。
    ERROR_LEVEL,
    CRITICAL,
    OFF
};

// 模块名独立于文件目标；所有模块共享同一组线程安全输出目标。
enum class Module {
    CORE,
    SERVICE,
    SERVER,
    SCPI,
    WEB,
    COUNT
};

struct LogConfig {
    Level level = Level::INFO;
    // 路径使用 UTF-8；Windows 后端转换为宽字符路径，避免受系统代码页影响。
    std::string filePath = "logs/vnapro.log";
    bool fileEnabled = true;
    bool consoleEnabled = true;
    std::size_t maxFileBytes = 10U * 1024U * 1024U;
    // 此处只计算备份文件，活动文件另占一份。
    std::size_t maxFiles = 5U;
    std::size_t maxRecordBytes = 64U * 1024U;
    bool rotateOnOpen = false;
    Level flushLevel = Level::INFO;
    // %K 是本模块扩展的大写等级字段，其他字段沿用 spdlog。
    std::string pattern = "%Y-%m-%d %H:%M:%S.%e [%K] [%t] %n - %v";
};

// 仅检查取值范围与字段约束，不创建文件；非法配置抛出 invalid_argument。
void ValidateConfig(const LogConfig &config);

}  // 命名空间 VnaLog

#endif
