#include "VnaLogConfiguration.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "PosixDriverFile.h"

namespace vna::detail {
namespace {

    constexpr std::size_t maximumConfigurationBytes = 64U * 1024U;
    constexpr std::size_t maximumLineBytes = 4096;
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

    std::string trim(std::string value)
    {
        const auto visible = [](unsigned char character) { return std::isspace(character) == 0; };
        const auto first = std::find_if(value.begin(), value.end(), visible);
        const auto last = std::find_if(value.rbegin(), value.rend(), visible).base();
        return first < last ? std::string(first, last) : std::string{};
    }

    struct TrustedConfigurationFile {
        std::string path;
        std::string content;
    };

    bool trustedConfigurationAttributes(const struct stat &attributes)
    {
        const bool trustedOwner = (attributes.st_uid == 0) || (attributes.st_uid == geteuid());
        const bool externallyWritable = (attributes.st_mode & (S_IWGRP | S_IWOTH)) != 0;
        const bool validSize =
            (attributes.st_size >= 0) && (static_cast<std::size_t>(attributes.st_size) <= maximumConfigurationBytes);
        return S_ISREG(attributes.st_mode) && trustedOwner && !externallyWritable && validSize;
    }

    Result<std::string> readConfigurationContent(int descriptor)
    {
        // 分块读取并逐块核对总长度，防止配置在打开后被扩写导致无界内存分配。
        std::array<char, 4096> buffer{};
        std::string content;
        for (;;) {
            const ssize_t count = read(descriptor, buffer.data(), buffer.size());
            if ((count < 0) && (errno == EINTR)) {
                continue;
            }
            if (count < 0) {
                return Result<std::string>::failure(configurationError("logging configuration cannot be read"));
            }
            if (count == 0) {
                break;
            }
            const std::size_t received = static_cast<std::size_t>(count);
            if (received > (maximumConfigurationBytes - content.size())) {
                return Result<std::string>::failure(configurationError("logging configuration is too large"));
            }
            content.append(buffer.data(), received);
        }
        return Result<std::string>::success(std::move(content));
    }

    Result<TrustedConfigurationFile> readTrustedConfiguration(const std::string &source)
    {
        // 先规范化路径用于相对日志路径解析，再把信任校验和读取绑定到同一描述符。
        char resolved[PATH_MAX]{};
        if (realpath(source.c_str(), resolved) == nullptr) {
            return Result<TrustedConfigurationFile>::failure(
                configurationError("logging configuration cannot be resolved"));
        }
        // O_NOFOLLOW 与 fstat 让后续读取绑定到已经校验的同一个文件对象，避免路径检查后被替换。
        PosixFileDescriptor descriptor(open(resolved, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
        struct stat attributes{};
        if ((fstat(descriptor.get(), &attributes) != 0) || !trustedConfigurationAttributes(attributes)) {
            return Result<TrustedConfigurationFile>::failure(
                configurationError("logging configuration is not trusted"));
        }
        auto content = readConfigurationContent(descriptor.get());
        if (!content.hasValue()) {
            return Result<TrustedConfigurationFile>::failure(content.error());
        }
        return Result<TrustedConfigurationFile>::success(TrustedConfigurationFile{resolved, content.takeValue()});
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

    Result<std::size_t> parseSize(const std::string &text, std::size_t minimum, std::size_t maximum)
    {
        errno = 0;
        char *end = nullptr;
        const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
        const bool consumed = (end != text.c_str()) && (end != nullptr) && (*end == '\0');
        if ((errno == ERANGE) || !consumed || (value < minimum) || (value > maximum)) {
            return Result<std::size_t>::failure(configurationError("logging configuration has an invalid number"));
        }
        return Result<std::size_t>::success(static_cast<std::size_t>(value));
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

    using ConfigurationValues = std::map<std::string, std::string>;

    struct ParserState {
        std::string section;
        ConfigurationValues values;
    };

    Result<bool> insertLoggingEntry(const std::string &line, ConfigurationValues &values)
    {
        // 重复键通常意味着配置合并错误，拒绝而不是采用难以察觉的“最后一个值”。
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            return Result<bool>::failure(configurationError("logging configuration entry is malformed"));
        }
        std::string key = trim(line.substr(0, separator));
        std::string value = trim(line.substr(separator + 1));
        if (key.empty() || value.empty() || !values.emplace(std::move(key), std::move(value)).second) {
            return Result<bool>::failure(
                configurationError("logging configuration contains an empty or duplicate entry"));
        }
        return Result<bool>::success(true);
    }

    Result<bool> consumeConfigurationLine(std::string line, ParserState &state)
    {
        if (line.size() > maximumLineBytes) {
            return Result<bool>::failure(configurationError("logging configuration line is too long"));
        }
        line = trim(std::move(line));
        if (line.empty() || (line.front() == '#') || (line.front() == ';')) {
            return Result<bool>::success(true);
        }
        if ((line.front() == '[') && (line.back() == ']')) {
            state.section = trim(line.substr(1, line.size() - 2));
            return Result<bool>::success(true);
        }
        return state.section == "logging" ? insertLoggingEntry(line, state.values) : Result<bool>::success(true);
    }

    Result<ConfigurationValues> readLoggingSection(const std::string &content)
    {
        // 当前只消费 logging 节，允许同一个 conf 文件继续承载其他模块配置。
        std::istringstream input(content);
        ParserState state;
        std::string line;
        while (std::getline(input, line)) {
            auto consumed = consumeConfigurationLine(std::move(line), state);
            if (!consumed.hasValue()) {
                return Result<ConfigurationValues>::failure(consumed.error());
            }
        }
        if (!input.eof()) {
            return Result<ConfigurationValues>::failure(configurationError("logging configuration cannot be read"));
        }
        return Result<ConfigurationValues>::success(std::move(state.values));
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
        auto maximumBytes = parseSize(values.at("max_file_bytes"), minimumLogFileBytes, maximumLogFileBytes);
        if (!maximumBytes.hasValue()) {
            return Result<VnaLogConfiguration>::failure(maximumBytes.error());
        }
        auto maximumFiles = parseSize(values.at("max_files"), 1, maximumRotatedFiles);
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
    auto trustedFile = readTrustedConfiguration(configurationFile);
    if (!trustedFile.hasValue()) {
        return Result<VnaLogConfiguration>::failure(trustedFile.error());
    }
    auto values = readLoggingSection(trustedFile.value().content);
    if (!values.hasValue()) {
        return Result<VnaLogConfiguration>::failure(values.error());
    }
    return buildConfiguration(trustedFile.value().path, values.value());
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
