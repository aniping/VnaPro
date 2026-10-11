/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "utf8_argument.h"

#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// shellapi.h 依赖 windows.h 的基础类型，必须保留包含顺序。
#include <shellapi.h>

#include <memory>

namespace {
std::string ToUtf8(const wchar_t *value)
{
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        throw std::runtime_error("Cannot encode command argument as UTF-8");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &result[0], size, nullptr, nullptr) == 0) {
        throw std::runtime_error("Cannot encode command argument as UTF-8");
    }
    result.pop_back();
    return result;
}
}  // 匿名命名空间
#endif

namespace VnaServer {
std::string Utf8Argument(int index, const char *fallback)
{
#ifdef _WIN32
    static_cast<void>(fallback);
    int count = 0;
    wchar_t **values = CommandLineToArgvW(GetCommandLineW(), &count);
    const auto release = [](wchar_t **arguments) {
        LocalFree(arguments);
    };
    const std::unique_ptr<wchar_t *, decltype(release)> owner(values, release);
    if (values == nullptr || index < 0 || index >= count) {
        throw std::runtime_error("Cannot read Unicode command argument");
    }
    return ToUtf8(values[index]);
#else
    static_cast<void>(index);
    return fallback;
#endif
}
}  // 命名空间 VnaServer
