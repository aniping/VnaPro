#include "VnaLogConfiguration.h"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "VnaConfigurationFile.h"

namespace vna::detail {
namespace {

    constexpr std::size_t minimumLogFileBytes = 1024;
    constexpr std::size_t maximumLogFileBytes = 1024U * 1024U * 1024U;
    constexpr std::size_t maximumRotatedFiles = 100;

    VnaError configurationError(std::string description)
    {
        VnaError error;
        error.code = VnaErrorCode::ConfigurationRejected;
        error.description = std::move(description);
        return error;
    }

    std::string parentDirectory(const std::string &path)
    {
        const std::size_t separator = path.find_last_of('/');
        return separator == std::string::npos ? std::string{"."} : path.substr(0, separator);
    }

    std::string processIdentifier()
    {
#if defined(_WIN32)
        return std::to_string(_getpid());
#else
        return std::to_string(getpid());
#endif
    }

    std::string expandedLogPath(const std::string &configurationFile, std::string path)
    {
        // 进程号属于部署期信息，在解析配置时一次性展开，后端无需理解占位符。
        constexpr const char *processToken = "{pid}";
        for (std::size_t position = path.find(processToken); position != std::string::npos;
             position = path.find(processToken, position)) {
            const std::string identifier = processIdentifier();
            path.replace(position, std::char_traits<char>::length(processToken), identifier);
            position += identifier.size();
        }
        if (!path.empty() && (path.front() != '/')) {
            path = parentDirectory(configurationFile) + "/" + path;
        }
        return path;
    }

    Result<VnaLogLevel> parseLevel(const std::string &value)
    {
        static const std::map<std::string, VnaLogLevel> levels{
            {"trace", VnaLogLevel::Trace},     {"debug", VnaLogLevel::Debug}, {"info", VnaLogLevel::Info},
            {"warning", VnaLogLevel::Warning}, {"error", VnaLogLevel::Error}, {"critical", VnaLogLevel::Critical},
            {"off", VnaLogLevel::Off}};
        const auto match = levels.find(value);
        if (match == levels.end()) {
            return Result<VnaLogLevel>::failure(configurationError("logging configuration has an invalid level"));
        }
        return Result<VnaLogLevel>::success(match->second);
    }

    bool hasCompleteKeySet(const ConfigurationValues &values)
    {
        // 字段集合必须完全相等；拼错的键不能被静默忽略后退回默认值。
        static constexpr std::array<const char *, 5> keys{
            "file", "level", "flush_level", "max_file_bytes", "max_files"};
        if (values.size() != keys.size()) {
            return false;
        }
        return std::all_of(keys.begin(), keys.end(), [&values](const char *key) { return values.count(key) == 1; });
    }

    Result<VnaLogConfiguration> buildConfiguration(std::string path, const ConfigurationValues &values)
    {
        if (!hasCompleteKeySet(values)) {
            return Result<VnaLogConfiguration>::failure(
                configurationError("logging configuration keys are incomplete or unknown"));
        }
        auto level = parseLevel(values.at("level"));
        if (!level.hasValue()) {
            return Result<VnaLogConfiguration>::failure(level.error());
        }
        auto flushLevel = parseLevel(values.at("flush_level"));
        if (!flushLevel.hasValue()) {
            return Result<VnaLogConfiguration>::failure(flushLevel.error());
        }
        auto maximumBytes = parseConfigurationSize(
            values.at("max_file_bytes"), minimumLogFileBytes, maximumLogFileBytes, "logging file size");
        if (!maximumBytes.hasValue()) {
            return Result<VnaLogConfiguration>::failure(maximumBytes.error());
        }
        auto maximumFiles =
            parseConfigurationSize(values.at("max_files"), 1, maximumRotatedFiles, "logging file count");
        if (!maximumFiles.hasValue()) {
            return Result<VnaLogConfiguration>::failure(maximumFiles.error());
        }
        VnaLogConfiguration result;
        result.configurationFile = std::move(path);
        result.logFile = expandedLogPath(result.configurationFile, values.at("file"));
        result.level = level.takeValue();
        result.flushLevel = flushLevel.takeValue();
        result.maximumFileBytes = maximumBytes.takeValue();
        result.maximumFiles = maximumFiles.takeValue();
        return Result<VnaLogConfiguration>::success(std::move(result));
    }

}  // namespace

Result<VnaLogConfiguration> loadVnaLogConfiguration(const std::string &configurationFile)
{
    auto trustedFile = readTrustedConfigurationFile(configurationFile);
    if (!trustedFile.hasValue()) {
        return Result<VnaLogConfiguration>::failure(trustedFile.error());
    }
    return loadVnaLogConfiguration(trustedFile.value());
}

Result<VnaLogConfiguration> loadVnaLogConfiguration(const TrustedConfigurationFile &trustedFile)
{
    auto values = readConfigurationSection(trustedFile.content, "logging");
    if (!values.hasValue()) {
        return Result<VnaLogConfiguration>::failure(values.error());
    }
    return buildConfiguration(trustedFile.path, values.value());
}

bool sameLogConfiguration(const VnaLogConfiguration &left, const VnaLogConfiguration &right)
{
    return (left.logFile == right.logFile) && (left.level == right.level) && (left.flushLevel == right.flushLevel) &&
           (left.maximumFileBytes == right.maximumFileBytes) && (left.maximumFiles == right.maximumFiles);
}

const char *logLevelName(VnaLogLevel level)
{
    static const char *names[]{"trace", "debug", "info", "warning", "error", "critical", "off"};
    return names[static_cast<std::size_t>(level)];
}

}  // namespace vna::detail
