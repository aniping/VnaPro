#ifndef VNA_HARDWARE_VNA_LOG_CONFIGURATION_H
#define VNA_HARDWARE_VNA_LOG_CONFIGURATION_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Result.h"

namespace vna::detail {

struct TrustedConfigurationFile;

// 日志级别按严重程度递增，flushLevel 表示达到该级别时立即刷盘。
enum class VnaLogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error, Critical, Off };

struct VnaLogConfiguration {
    // configurationFile 保存规范化后的配置路径，用于诊断和多管理器一致性检查。
    std::string configurationFile;
    // logFile 已把相对路径和 {pid} 展开为可直接交给日志后端的路径。
    std::string logFile;
    VnaLogLevel level{VnaLogLevel::Info};
    VnaLogLevel flushLevel{VnaLogLevel::Warning};
    std::size_t maximumFileBytes{10U * 1024U * 1024U};
    std::size_t maximumFiles{5};
};

Result<VnaLogConfiguration> loadVnaLogConfiguration(const std::string &configurationFile);
Result<VnaLogConfiguration> loadVnaLogConfiguration(const TrustedConfigurationFile &configurationFile);
bool sameLogConfiguration(const VnaLogConfiguration &left, const VnaLogConfiguration &right);
const char *logLevelName(VnaLogLevel level);

}  // namespace vna::detail

#endif
