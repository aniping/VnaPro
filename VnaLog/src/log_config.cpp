/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "vna_log/log_config.h"

#include <cstdint>
#include <stdexcept>

namespace {

constexpr std::uint64_t MAX_FILE_BYTES = 4ULL * 1024ULL * 1024ULL * 1024ULL;

bool ValidLevel(VnaLog::Level level)
{
    return level >= VnaLog::Level::TRACE && level <= VnaLog::Level::OFF;
}

}  // 匿名命名空间

namespace VnaLog {

void ValidateConfig(const LogConfig &config)
{
    if (!ValidLevel(config.level)) {
        throw std::invalid_argument("Invalid log level");
    }
    if (!ValidLevel(config.flushLevel)) {
        throw std::invalid_argument("Invalid log flush_level");
    }
    if (config.fileEnabled && config.filePath.empty()) {
        throw std::invalid_argument("Log file_path must not be empty when file_enabled is true");
    }
    if (config.filePath.find('\0') != std::string::npos) {
        throw std::invalid_argument("Log file_path must not contain NUL");
    }
    if (config.maxFileBytes < 1024U || config.maxFileBytes > MAX_FILE_BYTES) {
        throw std::invalid_argument("Log max_file_bytes must be between 1024 and 4294967296");
    }
    if (config.maxRecordBytes < 32U || config.maxRecordBytes > MAX_FILE_BYTES) {
        throw std::invalid_argument("Log max_record_bytes must be between 32 and 4294967296");
    }
    if (config.maxFiles > 1000U) {
        throw std::invalid_argument("Log max_files must not exceed 1000");
    }
    if (config.pattern.empty() || config.pattern.size() > 1024U ||
        config.pattern.find_first_of("\r\n") != std::string::npos) {
        throw std::invalid_argument("Log pattern must contain 1 to 1024 bytes without line breaks");
    }
}

}  // 命名空间 VnaLog
