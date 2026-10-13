/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/static_file.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace VnaWeb {
namespace {
constexpr std::size_t OUTPUT_LIMIT = 4U * 1024U * 1024U;
constexpr unsigned STATIC_READ_BUDGET = 16384U;
#ifdef _WIN32
constexpr std::size_t MAX_WINDOWS_PATH_CHARACTERS = 32768U;

std::wstring WindowsPath(const std::string &path)
{
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("静态文件路径为空或过长");
    }
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    if (length == 0) {
        throw std::runtime_error("静态文件路径不是有效 UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) != length) {
        throw std::runtime_error("静态文件路径转换失败");
    }
    return result;
}

std::string Utf8Path(const wchar_t *path, int characters)
{
    const int length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, characters, nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        throw std::runtime_error("静态文件规范路径转换失败");
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, characters, &result[0], length, nullptr, nullptr) !=
        length) {
        throw std::runtime_error("静态文件规范路径转换失败");
    }
    return result;
}

void CloseDirectory(HANDLE directory) noexcept
{
    if (!CloseHandle(directory)) {
        std::fputs("Static directory close failed\n", stderr);
    }
}

std::string Lower(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}
#endif
}  // 匿名命名空间

StaticFile::StaticFile(const std::string &root, const std::string &path)
{
    Open(root, path);
}

void StaticFile::Open(const std::string &root, const std::string &path)
{
    Close();
    if (root.empty() || path.empty() || path.front() != '/' || root.find('\0') != std::string::npos ||
        path.find('\0') != std::string::npos) {
        throw std::runtime_error("Static files are disabled or the path is invalid");
    }
    std::string relative = path == "/" ? "index.html" : path.substr(1U);
    if (relative.empty() || relative.back() == '/') {
        throw std::runtime_error("Static file not found");
    }
    std::istringstream parts(relative);
    std::string part;
    while (std::getline(parts, part, '/')) {
        if (part.empty() || part == "." || part == ".." || part.find_first_of("\\:") != std::string::npos) {
            throw std::runtime_error("Invalid static path");
        }
    }
    try {
        OpenVerified(root, relative);
    } catch (...) {
        Close();
        throw;
    }
}

#ifdef _WIN32
void StaticFile::OpenVerified(const std::string &root, std::string relative)
{
    const HANDLE directory = CreateFileW(WindowsPath(root).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Static root unavailable");
    }
    std::string rootPath;
    try {
        rootPath = FinalPath(directory);
    } catch (...) {
        CloseDirectory(directory);
        throw;
    }
    CloseDirectory(directory);
    std::replace(relative.begin(), relative.end(), '/', '\\');
    const std::string fullPath = root + "\\" + relative;
    file_ = CreateFileW(WindowsPath(fullPath).c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Static file not found");
    }
    VerifyFile(rootPath);
}

void StaticFile::VerifyFile(const std::string &rootPath)
{
    const std::string actual = Lower(FinalPath(file_));
    const std::string prefix = Lower(rootPath) + "\\";
    if (actual.compare(0, prefix.size(), prefix) != 0) {
        throw std::runtime_error("Static file escapes root");
    }
    LARGE_INTEGER length{};
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileSizeEx(file_, &length) || !GetFileInformationByHandle(file_, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U || length.QuadPart < 0 ||
        static_cast<unsigned long long>(length.QuadPart) > OUTPUT_LIMIT) {
        throw std::runtime_error("Static file is unavailable or oversized");
    }
    size_ = static_cast<std::size_t>(length.QuadPart);
}
#else
void StaticFile::OpenVerified(const std::string &root, const std::string &relative)
{
    file_ = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (file_ < 0) {
        throw std::runtime_error("Static root unavailable");
    }
    std::istringstream parts(relative);
    std::string segment;
    while (std::getline(parts, segment, '/')) {
        const bool final = parts.peek() == std::char_traits<char>::eof();
        const int next =
            openat(file_, segment.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (final ? O_NONBLOCK : O_DIRECTORY));
        if (close(file_) != 0) {
            std::fputs("Static directory close failed\n", stderr);
        }
        file_ = next;
        if (file_ < 0) {
            throw std::runtime_error("Static file not found");
        }
    }

    struct stat information {};

    if (fstat(file_, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size < 0 ||
        static_cast<unsigned long long>(information.st_size) > OUTPUT_LIMIT) {
        throw std::runtime_error("Invalid static file");
    }
    size_ = static_cast<std::size_t>(information.st_size);
}
#endif

StaticFile::~StaticFile() noexcept
{
    Close();
}

void StaticFile::Close() noexcept
{
#ifdef _WIN32
    if (file_ != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(file_)) {
            std::fputs("Static file close failed\n", stderr);
        }
        file_ = INVALID_HANDLE_VALUE;
    }
#else
    if (file_ >= 0) {
        if (close(file_) != 0) {
            std::fputs("Static file close failed\n", stderr);
        }
        file_ = -1;
    }
#endif
    size_ = 0U;
    readOffset_ = 0U;
}

std::size_t StaticFile::Size() const noexcept
{
    return size_;
}

bool StaticFile::Advance(std::string &output)
{
    try {
#ifdef _WIN32
        const bool closed = file_ == INVALID_HANDLE_VALUE;
#else
        const bool closed = file_ < 0;
#endif
        if (closed || output.size() != readOffset_) {
            throw std::runtime_error("Static file is closed or the output buffer changed");
        }
        if (readOffset_ == size_) {
            return true;
        }
        char buffer[STATIC_READ_BUDGET]{};
        const unsigned maximum = static_cast<unsigned>((std::min)(size_ - readOffset_, sizeof(buffer)));
        const std::size_t count = ReadAvailable(buffer, maximum);
        output.append(buffer, count);
        readOffset_ += count;
        return readOffset_ == size_;
    } catch (...) {
        Close();
        throw;
    }
}

std::size_t StaticFile::ReadAvailable(char *buffer, unsigned maximum)
{
#ifdef _WIN32
    DWORD count = 0U;
    if (!ReadFile(file_, buffer, maximum, &count, nullptr) || count == 0U) {
        throw std::runtime_error("Static file read failed");
    }
#else
    ssize_t count = 0;
    do {
        count = read(file_, buffer, maximum);
    } while (count < 0 && errno == EINTR);
    if (count <= 0) {
        throw std::runtime_error("Static file read failed");
    }
#endif
    return static_cast<std::size_t>(count);
}

#ifdef _WIN32
std::string StaticFile::FinalPath(HANDLE file)
{
    std::vector<wchar_t> buffer(MAX_WINDOWS_PATH_CHARACTERS);
    const DWORD count =
        GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    if (count == 0U || count >= buffer.size()) {
        throw std::runtime_error("Cannot verify static file path");
    }
    std::string result = Utf8Path(buffer.data(), static_cast<int>(count));
    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    return result;
}
#endif
}  // 命名空间 VnaWeb
