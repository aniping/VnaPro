/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/dynamic_library.h"

#include <cctype>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace VnaCore {
namespace Detail {
namespace {

bool IsAbsolutePath(const std::string &path)
{
#if defined(_WIN32)
    const bool driveRooted = path.size() >= 3U && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' &&
                             (path[2] == '/' || path[2] == '\\');
    const bool unc = path.size() >= 5U && path[0] == '\\' && path[1] == '\\' && path[2] != '\\';
    return driveRooted || unc;
#else
    return !path.empty() && path[0] == '/';
#endif
}

#if defined(_WIN32)
std::wstring DecodePath(const std::string &path)
{
    if (path.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("native library path is too long");
    }
    const int length = static_cast<int>(path.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
    if (count == 0) {
        throw std::runtime_error("native library path must be valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, &result[0], count) != count) {
        throw std::runtime_error("native library path decoding failed");
    }
    return result;
}
#endif

}  // 匿名命名空间

DynamicLibrary::~DynamicLibrary() noexcept
{
    Close();
}

void DynamicLibrary::Open(const std::string &path)
{
    if (!IsAbsolutePath(path) || path.find('\0') != std::string::npos) {
        throw std::runtime_error("native library path must be absolute and contain no null character");
    }
    if (handle_) {
        return;
    }
#if defined(_WIN32)
    const std::wstring widePath = DecodePath(path);
    handle_ = static_cast<void *>(LoadLibraryExW(widePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
    if (!handle_) {
        throw std::runtime_error("LoadLibraryEx failed, error=" + std::to_string(GetLastError()));
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char *error = dlerror();
        throw std::runtime_error(error ? error : "dlopen failed");
    }
#endif
}

void *DynamicLibrary::FindSymbol(const char *name) const
{
    void *symbol = nullptr;
    if (handle_) {
#if defined(_WIN32)
        const FARPROC address = GetProcAddress(static_cast<HMODULE>(handle_), name);
        static_assert(sizeof(symbol) == sizeof(address), "Native symbol pointer size mismatch");
        std::memcpy(&symbol, &address, sizeof(symbol));
#else
        symbol = dlsym(handle_, name);
#endif
    }
    if (!symbol) {
        throw std::runtime_error(std::string("native symbol missing: ") + name);
    }
    return symbol;
}

void DynamicLibrary::Close() noexcept
{
    if (handle_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
