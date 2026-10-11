/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "server_config_io.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace VnaServer {
namespace ConfigIo {
namespace {
constexpr std::size_t MAX_CONFIG_BYTES = 64U * 1024U;
constexpr unsigned UTF8_ASCII_LIMIT = 0x80U;
constexpr unsigned UTF8_TWO_BYTE_BEGIN = 0xc2U;
constexpr unsigned UTF8_TWO_BYTE_END = 0xdfU;
constexpr unsigned UTF8_THREE_BYTE_BEGIN = 0xe0U;
constexpr unsigned UTF8_THREE_BYTE_END = 0xefU;
constexpr unsigned UTF8_FOUR_BYTE_BEGIN = 0xf0U;
constexpr unsigned UTF8_FOUR_BYTE_END = 0xf4U;
constexpr unsigned UTF8_CONTINUATION_MASK = 0xc0U;
constexpr unsigned UTF8_CONTINUATION_TAG = 0x80U;
constexpr unsigned UTF8_PAYLOAD_MASK = 0x3fU;
constexpr unsigned UTF8_PAYLOAD_BITS = 6U;
constexpr std::uint32_t MAX_UNICODE_CODEPOINT = 0x10ffffU;
constexpr std::uint32_t SURROGATE_BEGIN = 0xd800U;
constexpr std::uint32_t SURROGATE_END = 0xdfffU;

unsigned FollowingBytes(unsigned first) noexcept
{
    if (first >= UTF8_TWO_BYTE_BEGIN && first <= UTF8_TWO_BYTE_END) {
        return 1U;
    }
    if (first >= UTF8_THREE_BYTE_BEGIN && first <= UTF8_THREE_BYTE_END) {
        return 2U;
    }
    if (first >= UTF8_FOUR_BYTE_BEGIN && first <= UTF8_FOUR_BYTE_END) {
        return 3U;
    }
    return 0U;
}

void ValidateUtf8(const std::string &text)
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index++]);
        if (first > 0U && first < UTF8_ASCII_LIMIT) {
            continue;
        }
        const unsigned following = FollowingBytes(first);
        if (following == 0U || text.size() - index < following) {
            throw std::invalid_argument("配置文本必须是有效 UTF-8，且不含空字符");
        }
        std::uint32_t point = first & ((1U << (UTF8_PAYLOAD_BITS - following)) - 1U);
        for (unsigned offset = 0; offset < following; ++offset) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_TAG) {
                throw std::invalid_argument("配置文本包含非法 UTF-8 延续字节");
            }
            point = (point << UTF8_PAYLOAD_BITS) | (next & UTF8_PAYLOAD_MASK);
        }
        // 索引为延续字节数，对应合法二、三、四字节编码的最小 Unicode 码点。
        const std::uint32_t minimum[] = {0U, 0x80U, 0x800U, 0x10000U};
        if (point < minimum[following] || point > MAX_UNICODE_CODEPOINT ||
            (point >= SURROGATE_BEGIN && point <= SURROGATE_END)) {
            throw std::invalid_argument("配置文本包含非法 UTF-8 码点");
        }
    }
}

#ifdef _WIN32
std::wstring WindowsPath(const std::string &path)
{
    ValidateUtf8(path);
    if (path.empty() || path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("配置路径为空或过长");
    }
    const auto bytes = static_cast<int>(path.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, nullptr, 0);
    if (length == 0) {
        throw std::invalid_argument("配置路径必须为有效的 UTF-8 文本");
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), bytes, &result[0], length) != length) {
        throw std::runtime_error("配置路径转换失败");
    }
    return result;
}
#endif

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
}  // 匿名命名空间

std::string ReadUtf8(const std::string &path)
{
#ifdef _WIN32
    std::ifstream file(WindowsPath(path).c_str(), std::ios::binary);
#else
    ValidateUtf8(path);
    std::ifstream file(path, std::ios::binary);
#endif
    if (!file) {
        throw std::runtime_error("无法读取服务配置文件：" + path);
    }
    // 多读取一个字节，防止超限文件被静默截断。
    std::array<char, MAX_CONFIG_BYTES + 1U> buffer{};
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.bad() || (file.fail() && !file.eof())) {
        throw std::runtime_error("读取服务配置文件失败：" + path);
    }
    const auto count = static_cast<std::size_t>(file.gcount());
    if (count > MAX_CONFIG_BYTES) {
        throw std::invalid_argument("服务配置文件超过 64 KiB：" + path);
    }
    std::string text(buffer.data(), count);
    ValidateUtf8(text);
    if (text.compare(0, 3, "\xef\xbb\xbf") == 0) {
        text.erase(0, 3);
    }
    return text;
}

std::string ResolveFilePath(const std::string &filePath, const std::string &configPath)
{
    if (IsAbsolutePath(filePath)) {
        return filePath;
    }
    const auto separator = configPath.find_last_of("/\\");
    return separator == std::string::npos ? filePath : configPath.substr(0, separator + 1U) + filePath;
}

}  // 命名空间 ConfigIo
}  // 命名空间 VnaServer
