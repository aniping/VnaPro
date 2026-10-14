/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_array_json.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace VnaWeb {
namespace {
constexpr std::size_t DOUBLE_BYTES = 8U;
constexpr unsigned BYTE_BITS = 8U;
constexpr std::uint64_t BYTE_MASK = 0xffU;
constexpr std::uint64_t NULL_BITS = UINT64_C(0x7ff8000000000000);
constexpr std::size_t ENUM_LIMIT = 256U;
constexpr unsigned BASE64_BITS = 6U;
constexpr unsigned BASE64_MASK = 0x3fU;
constexpr unsigned BASE64_BLOCK_BYTES = 3U;
constexpr unsigned BASE64_BLOCK_CHARACTERS = 4U;

static_assert(sizeof(double) == DOUBLE_BYTES && std::numeric_limits<double>::is_iec559,
    "Frame array codec requires IEEE 754 binary64");

std::string Base64(const std::vector<unsigned char> &bytes)
{
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const std::size_t groups = bytes.size() / BASE64_BLOCK_BYTES + (bytes.size() % BASE64_BLOCK_BYTES != 0U);
    std::string result;
    if (groups > result.max_size() / BASE64_BLOCK_CHARACTERS) {
        throw std::length_error("Frame array Base64 exceeds string capacity");
    }
    result.reserve(groups * BASE64_BLOCK_CHARACTERS);
    for (std::size_t first = 0U; first < bytes.size(); first += BASE64_BLOCK_BYTES) {
        const std::size_t count = std::min<std::size_t>(BASE64_BLOCK_BYTES, bytes.size() - first);
        unsigned bits = static_cast<unsigned>(bytes[first]) << (BYTE_BITS * 2U);
        if (count > 1U) {
            bits |= static_cast<unsigned>(bytes[first + 1U]) << BYTE_BITS;
        }
        if (count > 2U) {
            bits |= bytes[first + 2U];
        }
        result.push_back(alphabet[(bits >> (BASE64_BITS * 3U)) & BASE64_MASK]);
        result.push_back(alphabet[(bits >> (BASE64_BITS * 2U)) & BASE64_MASK]);
        result.push_back(count > 1U ? alphabet[(bits >> BASE64_BITS) & BASE64_MASK] : '=');
        result.push_back(count > 2U ? alphabet[bits & BASE64_MASK] : '=');
    }
    return result;
}

Json Envelope(const char *codec, std::size_t count, const std::vector<unsigned char> &bytes)
{
    Json packed = Json::MakeObject();
    packed["codec"] = Json(codec);
    packed["count"] = Json(static_cast<double>(count));
    Json data;
    data.type = Json::Type::STRING;
    data.string = Base64(bytes);
    packed["data"] = std::move(data);
    return packed;
}

Json PackNumbers(const std::vector<Json> &values)
{
    std::vector<unsigned char> bytes;
    if (values.size() > bytes.max_size() / DOUBLE_BYTES) {
        throw std::length_error("Frame numeric array exceeds byte capacity");
    }
    bytes.reserve(values.size() * DOUBLE_BYTES);
    for (const auto &value : values) {
        std::uint64_t bits = NULL_BITS;
        if (value.type == Json::Type::NUMBER) {
            if (!std::isfinite(value.number)) {
                throw std::invalid_argument("Frame numeric array contains a non-finite value");
            }
            std::memcpy(&bits, &value.number, sizeof(bits));
        }
        // 显式按低位字节写出，保持负零和完整精度，不依赖主机字节序。
        for (unsigned byte = 0U; byte < DOUBLE_BYTES; ++byte) {
            bytes.push_back(static_cast<unsigned char>((bits >> (byte * BYTE_BITS)) & BYTE_MASK));
        }
    }
    return Envelope("f64le", values.size(), bytes);
}

Json PackBooleans(const std::vector<Json> &values)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(values.size());
    for (const auto &value : values) {
        bytes.push_back(value.boolean ? 1U : 0U);
    }
    return Envelope("u8", values.size(), bytes);
}

Json PackStrings(Json value)
{
    std::map<std::string, unsigned char> indexes;
    Json dictionary = Json::MakeArray();
    std::vector<unsigned char> bytes;
    bytes.reserve(value.array.size());
    for (const auto &item : value.array) {
        const auto found = indexes.find(item.string);
        if (found != indexes.end()) {
            bytes.push_back(found->second);
            continue;
        }
        if (dictionary.array.size() == ENUM_LIMIT) {
            return value;
        }
        const auto index = static_cast<unsigned char>(dictionary.array.size());
        indexes.emplace(item.string, index);
        dictionary.array.push_back(Json(item.string));
        bytes.push_back(index);
    }
    Json packed = Envelope("enum8", value.array.size(), bytes);
    // 字典保持普通字符串数组，不能再次转换成 enum8。
    packed["dictionary"] = std::move(dictionary);
    return packed;
}
}  // 匿名命名空间

Json FrameArrayJson::Pack(Json value)
{
    if (value.type == Json::Type::OBJECT) {
        for (auto &member : value.object) {
            member.second = Pack(std::move(member.second));
        }
    } else if (value.type == Json::Type::ARRAY && !value.array.empty()) {
        const auto &values = value.array;
        if (std::all_of(values.begin(), values.end(), [](const Json &item) {
                return item.type == Json::Type::NUMBER || item.type == Json::Type::NULL_VALUE;
            })) {
            return PackNumbers(values);
        }
        if (std::all_of(values.begin(), values.end(), [](const Json &item) {
                return item.type == Json::Type::BOOLEAN;
            })) {
            return PackBooleans(values);
        }
        if (std::all_of(values.begin(), values.end(), [](const Json &item) {
                return item.type == Json::Type::STRING;
            })) {
            return PackStrings(std::move(value));
        }
        for (auto &item : value.array) {
            item = Pack(std::move(item));
        }
    }
    return value;
}
}  // 命名空间 VnaWeb
