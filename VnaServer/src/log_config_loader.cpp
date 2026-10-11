/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "log_config_loader.h"

#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VnaServer {
namespace {
constexpr std::size_t MAX_CONFIG_BYTES = 64U * 1024U;
constexpr std::size_t MAX_LINE_BYTES = 2048U;

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    if (path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        path.find('\0') != std::string::npos) {
        throw std::invalid_argument("日志配置路径过长或包含空字符");
    }
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    if (length == 0) {
        throw std::invalid_argument("日志配置路径必须为有效的 UTF-8 文本");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) != length) {
        throw std::runtime_error("日志配置路径转换失败");
    }
    return result;
}
#endif

std::string Trim(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string::npos) {
        return {};
    }
    return value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1U);
}

[[noreturn]] void Invalid(std::size_t line, const std::string &key, const std::string &reason)
{
    throw std::invalid_argument("日志配置第 " + std::to_string(line) + " 行，键 '" + key + "'：" + reason);
}

bool ParseBool(const std::string &value, std::size_t line, const std::string &key)
{
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    Invalid(line, key, "布尔值必须为 true 或 false");
}

std::size_t ParseSize(const std::string &value, std::size_t line, const std::string &key)
{
    if (value.empty()) {
        Invalid(line, key, "无符号十进制整数不能为空");
    }
    std::size_t result = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') {
            Invalid(line, key, "必须为无符号十进制整数");
        }
        const auto numeric = static_cast<std::size_t>(digit - '0');
        if (result > (std::numeric_limits<std::size_t>::max() - numeric) / 10U) {
            Invalid(line, key, "整数溢出");
        }
        result = result * 10U + numeric;
    }
    return result;
}

VnaLog::Level ParseLevel(const std::string &value, std::size_t line, const std::string &key)
{
    const std::array<std::pair<const char *, VnaLog::Level>, 7> levels{{{"TRACE", VnaLog::Level::TRACE},
        {"DEBUG", VnaLog::Level::DEBUG}, {"INFO", VnaLog::Level::INFO}, {"WARN", VnaLog::Level::WARN},
        {"ERROR", VnaLog::Level::ERROR_LEVEL}, {"CRITICAL", VnaLog::Level::CRITICAL}, {"OFF", VnaLog::Level::OFF}}};
    for (const auto &entry : levels) {
        if (value == entry.first) {
            return entry.second;
        }
    }
    Invalid(line, key, "日志级别不合法，必须使用大写名称");
}

bool ApplyBool(VnaLog::LogConfig &config, const std::string &key, const std::string &value, std::size_t line)
{
    const std::array<std::pair<const char *, bool VnaLog::LogConfig::*>, 3> fields{
        {{"console.enabled", &VnaLog::LogConfig::consoleEnabled}, {"file.enabled", &VnaLog::LogConfig::fileEnabled},
            {"file.rotate_on_open", &VnaLog::LogConfig::rotateOnOpen}}};
    for (const auto &field : fields) {
        if (key == field.first) {
            config.*field.second = ParseBool(value, line, key);
            return true;
        }
    }
    return false;
}

bool ApplySize(VnaLog::LogConfig &config, const std::string &key, const std::string &value, std::size_t line)
{
    const std::array<std::pair<const char *, std::size_t VnaLog::LogConfig::*>, 3> fields{
        {{"file.max_bytes", &VnaLog::LogConfig::maxFileBytes}, {"file.backups", &VnaLog::LogConfig::maxFiles},
            {"file.max_record_bytes", &VnaLog::LogConfig::maxRecordBytes}}};
    for (const auto &field : fields) {
        if (key == field.first) {
            config.*field.second = ParseSize(value, line, key);
            return true;
        }
    }
    return false;
}

void ApplyEntry(VnaLog::LogConfig &config, const std::string &key, const std::string &value, std::size_t line)
{
    if (ApplyBool(config, key, value, line) || ApplySize(config, key, value, line)) {
        return;
    }
    if (key == "level") {
        config.level = ParseLevel(value, line, key);
    } else if (key == "flush.level") {
        config.flushLevel = ParseLevel(value, line, key);
    } else if (key == "file.path") {
        if (value.empty()) {
            Invalid(line, key, "文件路径不能为空");
        }
        config.filePath = value;
    } else if (key == "pattern") {
        config.pattern = value;
    } else {
        Invalid(line, key, "未知配置键");
    }
}

std::string ReadProperties(const std::string &path)
{
#ifdef _WIN32
    // 显式从 UTF-8 转换到 Windows 宽路径，避免受到系统 ANSI 代码页影响。
    std::ifstream file(WindowsPath(path).c_str(), std::ios::binary);
#else
    std::ifstream file(path, std::ios::binary);
#endif
    if (!file) {
        throw std::runtime_error("无法读取日志配置文件：" + path);
    }
    // 多读一个字节，以拒绝超限文件而不是静默截断配置。
    std::array<char, MAX_CONFIG_BYTES + 1U> buffer{};
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.bad() || (file.fail() && !file.eof())) {
        throw std::runtime_error("读取日志配置文件失败：" + path);
    }
    const auto count = static_cast<std::size_t>(file.gcount());
    if (count > MAX_CONFIG_BYTES) {
        throw std::invalid_argument("日志配置文件超过 64 KiB：" + path);
    }
    std::string text(buffer.data(), count);
    // 支持常见 Windows 编辑器生成的 UTF-8 字节序标记。
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) {
        text.erase(0, 3);
    }
    return text;
}

void ParseLine(VnaLog::LogConfig &config, std::set<std::string> &seen, const std::string &rawLine, std::size_t line)
{
    if (rawLine.size() > MAX_LINE_BYTES) {
        Invalid(line, "<未解析>", "单行超过 2048 字节");
    }
    if (rawLine.find('\0') != std::string::npos) {
        Invalid(line, "<未解析>", "包含空字符");
    }
    const std::string text = Trim(rawLine);
    if (text.empty() || text.front() == '#') {
        return;
    }
    const auto delimiter = text.find('=');
    if (delimiter == std::string::npos) {
        Invalid(line, "<未解析>", "配置项必须使用 key=value");
    }
    const std::string key = Trim(text.substr(0, delimiter));
    if (key.empty()) {
        Invalid(line, "<未解析>", "配置键不能为空");
    }
    if (!seen.insert(key).second) {
        Invalid(line, key, "配置键重复");
    }
    ApplyEntry(config, key, Trim(text.substr(delimiter + 1U)), line);
    // 复用日志后端的范围约束，并补齐 properties 文件的字段定位信息。
    try {
        VnaLog::ValidateConfig(config);
    } catch (const std::invalid_argument &error) {
        Invalid(line, key, error.what());
    }
}

bool IsAbsolutePath(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return true;
    }
    const char drive = path.front();
    const bool hasDrive = (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
    return path.size() >= 3U && hasDrive && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
}

void ResolveFilePath(VnaLog::LogConfig &config, const std::string &configPath)
{
    if (IsAbsolutePath(config.filePath)) {
        return;
    }
    const auto separator = configPath.find_last_of("/\\");
    if (separator != std::string::npos) {
        config.filePath = configPath.substr(0, separator + 1U) + config.filePath;
    }
}
}  // 匿名命名空间

VnaLog::LogConfig LoadLogConfig(const std::string &path)
{
    VnaLog::LogConfig config;
    std::istringstream input(ReadProperties(path));
    std::set<std::string> seen;
    std::string rawLine;
    std::size_t line = 0;
    while (std::getline(input, rawLine)) {
        ParseLine(config, seen, rawLine, ++line);
    }
    ResolveFilePath(config, path);
    return config;
}
}  // 命名空间 VnaServer
