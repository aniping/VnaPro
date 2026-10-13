/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "startup_paths.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
#endif

namespace VnaServer {
namespace {
constexpr std::size_t INITIAL_PATH_CAPACITY = 256U;
constexpr std::size_t MAX_PATH_CAPACITY = 32768U;

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("启动资源路径为空或过长");
    }
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    if (length == 0) {
        throw std::invalid_argument("启动资源路径不是有效 UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) != length) {
        throw std::runtime_error("启动资源路径转换失败");
    }
    return result;
}

std::string ExecutablePath()
{
    std::vector<wchar_t> buffer(INITIAL_PATH_CAPACITY);
    while (buffer.size() <= MAX_PATH_CAPACITY) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0U) {
            throw std::runtime_error("无法定位正在运行的可执行文件");
        }
        if (length < buffer.size()) {
            const auto characters = static_cast<int>(length);
            const int bytes = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), characters, nullptr, 0, nullptr, nullptr);
            if (bytes == 0) {
                throw std::runtime_error("可执行文件路径转换失败");
            }
            std::string result(static_cast<std::size_t>(bytes), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), characters, &result[0], bytes,
                    nullptr, nullptr) != bytes) {
                throw std::runtime_error("可执行文件路径转换失败");
            }
            return result;
        }
        buffer.resize(buffer.size() * 2U);
    }
    throw std::runtime_error("可执行文件路径过长");
}
#else
std::string ExecutablePath()
{
    std::vector<char> buffer(INITIAL_PATH_CAPACITY);
#ifdef __APPLE__
    auto capacity = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &capacity) != 0) {
        if (capacity > MAX_PATH_CAPACITY) {
            throw std::runtime_error("可执行文件路径过长");
        }
        buffer.resize(capacity);
        if (_NSGetExecutablePath(buffer.data(), &capacity) != 0) {
            throw std::runtime_error("无法定位正在运行的可执行文件");
        }
    }
    return buffer.data();
#else
    while (buffer.size() <= MAX_PATH_CAPACITY) {
        const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) {
            throw std::runtime_error("无法定位正在运行的可执行文件");
        }
        if (static_cast<std::size_t>(length) < buffer.size()) {
            return std::string(buffer.data(), static_cast<std::size_t>(length));
        }
        buffer.resize(buffer.size() * 2U);
    }
    throw std::runtime_error("可执行文件路径过长");
#endif
}
#endif

bool ResourceExists(const std::string &path, StartupPathKind kind)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(WindowsPath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
    return kind == StartupPathKind::DIRECTORY ? directory : !directory;
#else
    struct stat information {};

    if (stat(path.c_str(), &information) != 0) {
        return false;
    }
    return kind == StartupPathKind::DIRECTORY ? S_ISDIR(information.st_mode) : S_ISREG(information.st_mode);
#endif
}
}  // 匿名命名空间

std::string ExecutableDirectory()
{
    std::string path = ExecutablePath();
    std::replace(path.begin(), path.end(), '\\', '/');
    const auto separator = path.find_last_of('/');
    if (separator == std::string::npos) {
        throw std::runtime_error("可执行文件目录不可用");
    }
    const bool driveRoot = separator == 2U && path[1] == ':';
    return path.substr(0, separator == 0U || driveRoot ? separator + 1U : separator);
}

std::string DefaultStartupPath(const std::string &executableDirectory, const std::string &adjacentRelativePath,
    const std::string &developmentFallback, StartupPathKind kind)
{
    const std::string path = executableDirectory + "/" + adjacentRelativePath;
    // 发布包缺少依赖必须显式失败，不能偷偷依赖编译机器上的旧文件。
    const bool releaseBundle = ResourceExists(executableDirectory + "/.vnapro-release-files", StartupPathKind::FILE);
    return releaseBundle || ResourceExists(path, kind) ? path : developmentFallback;
}
}  // 命名空间 VnaServer
