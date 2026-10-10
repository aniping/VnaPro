#include "web/StaticFile.h"

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
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vna_web {
namespace {
constexpr std::size_t kOutputLimit = 4U * 1024U * 1024U;
#ifdef _WIN32
std::string lower(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}
#endif
}  // namespace

StaticFile::StaticFile(const std::string &root, const std::string &path)
{
    if (root.empty()) {
        throw std::runtime_error("Static files are disabled");
    }
    std::string relative = path == "/" ? "index.html" : path.substr(1U);
    if (relative.empty() || relative.back() == '/') {
        throw std::runtime_error("Static file not found");
    }
#ifdef _WIN32
    HANDLE directory =
        CreateFileA(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Static root unavailable");
    }
    std::string rootPath;
    try {
        rootPath = finalPath(directory);
    } catch (...) {
        CloseHandle(directory);
        throw;
    }
    CloseHandle(directory);
    std::replace(relative.begin(), relative.end(), '/', '\\');
    const std::string fullPath = root + "\\" + relative;
    file_ = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Static file not found");
    }
    try {
        const std::string actual = lower(finalPath(file_));
        const std::string prefix = lower(rootPath) + "\\";
        if (actual.compare(0, prefix.size(), prefix) != 0) {
            throw std::runtime_error("Static file escapes root");
        }
        LARGE_INTEGER length;
        BY_HANDLE_FILE_INFORMATION information;
        if (!GetFileSizeEx(file_, &length) || !GetFileInformationByHandle(file_, &information) ||
            (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U || length.QuadPart < 0 ||
            static_cast<unsigned long long>(length.QuadPart) > kOutputLimit) {
            throw std::runtime_error("Static file is unavailable or oversized");
        }
        size_ = static_cast<std::size_t>(length.QuadPart);
    } catch (...) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
        throw;
    }
#else
    int directory = open(root.c_str(), O_RDONLY | O_DIRECTORY);
    if (directory < 0) {
        throw std::runtime_error("Static root unavailable");
    }
    std::istringstream parts(relative);
    std::vector<std::string> segments;
    std::string segment;
    while (std::getline(parts, segment, '/')) {
        if (segment.empty()) {
            close(directory);
            throw std::runtime_error("Invalid static path");
        }
        segments.push_back(segment);
    }
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const bool final = index + 1U == segments.size();
        const int next = openat(directory, segments[index].c_str(), O_RDONLY | O_NOFOLLOW | (final ? 0 : O_DIRECTORY));
        close(directory);
        directory = next;
        if (directory < 0) {
            throw std::runtime_error("Static file not found");
        }
    }
    struct stat information;
    if (fstat(directory, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size < 0 ||
        static_cast<unsigned long long>(information.st_size) > kOutputLimit) {
        close(directory);
        throw std::runtime_error("Invalid static file");
    }
    file_ = directory;
    size_ = static_cast<std::size_t>(information.st_size);
#endif
}

StaticFile::~StaticFile()
{
#ifdef _WIN32
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
    }
#else
    if (file_ >= 0) {
        close(file_);
    }
#endif
}

std::size_t StaticFile::size() const
{
    return size_;
}

bool StaticFile::advance(std::string &output)
{
    if (output.size() == size_) {
        return true;
    }
    char buffer[16384];
    const unsigned maximum = static_cast<unsigned>((std::min)(size_ - output.size(), sizeof(buffer)));
#ifdef _WIN32
    DWORD count = 0;
    if (!ReadFile(file_, buffer, maximum, &count, nullptr) || count == 0U) {
        throw std::runtime_error("Static file read failed");
    }
#else
    const ssize_t count = read(file_, buffer, maximum);
    if (count <= 0) {
        throw std::runtime_error("Static file read failed");
    }
#endif
    output.append(buffer, static_cast<std::size_t>(count));
    return output.size() == size_;
}

#ifdef _WIN32
std::string StaticFile::finalPath(void *file)
{
    std::vector<char> buffer(32768U);
    const DWORD count =
        GetFinalPathNameByHandleA(file, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    if (count == 0U || count >= buffer.size()) {
        throw std::runtime_error("Cannot verify static file path");
    }
    std::string result(buffer.data(), count);
    while (!result.empty() && (result.back() == '\\' || result.back() == '/')) {
        result.pop_back();
    }
    return result;
}
#endif
}  // namespace vna_web
