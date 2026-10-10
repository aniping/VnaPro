/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/json.h"

#include <cstddef>
#include <stdexcept>

namespace VnaWeb {
namespace {
constexpr unsigned ASCII_LIMIT = 0x80U;
constexpr unsigned UTF8_CONTINUATION_MASK = 0xc0U;
constexpr unsigned UTF8_CONTINUATION_TAG = 0x80U;
constexpr unsigned UTF8_PAYLOAD_MASK = 0x3fU;
constexpr unsigned UTF8_PAYLOAD_BITS = 6U;
constexpr unsigned UTF8_TWO_BYTE_BEGIN = 0xc2U;
constexpr unsigned UTF8_TWO_BYTE_END = 0xdfU;
constexpr unsigned UTF8_THREE_BYTE_BEGIN = 0xe0U;
constexpr unsigned UTF8_THREE_BYTE_END = 0xefU;
constexpr unsigned UTF8_FOUR_BYTE_BEGIN = 0xf0U;
constexpr unsigned UTF8_FOUR_BYTE_END = 0xf4U;
constexpr unsigned UTF8_TWO_BYTE_PAYLOAD_MASK = 0x1fU;
constexpr unsigned UTF8_THREE_BYTE_PAYLOAD_MASK = 0x0fU;
constexpr unsigned UTF8_FOUR_BYTE_PAYLOAD_MASK = 0x07U;
constexpr unsigned TWO_BYTE_SCALAR_BEGIN = 0x80U;
constexpr unsigned THREE_BYTE_SCALAR_BEGIN = 0x800U;
constexpr unsigned FOUR_BYTE_SCALAR_BEGIN = 0x10000U;
constexpr unsigned SURROGATE_BEGIN = 0xd800U;
constexpr unsigned SURROGATE_END = 0xdfffU;
constexpr unsigned UNICODE_SCALAR_END = 0x10ffffU;

const char *RequireString(const char *value)
{
    if (value == nullptr) {
        throw std::invalid_argument("JSON string must not be null");
    }
    return value;
}

unsigned Utf8ContinuationCount(unsigned lead, unsigned &scalar, unsigned &minimum)
{
    if (lead >= UTF8_TWO_BYTE_BEGIN && lead <= UTF8_TWO_BYTE_END) {
        scalar = lead & UTF8_TWO_BYTE_PAYLOAD_MASK;
        minimum = TWO_BYTE_SCALAR_BEGIN;
        return 1U;
    }
    if (lead >= UTF8_THREE_BYTE_BEGIN && lead <= UTF8_THREE_BYTE_END) {
        scalar = lead & UTF8_THREE_BYTE_PAYLOAD_MASK;
        minimum = THREE_BYTE_SCALAR_BEGIN;
        return 2U;
    }
    if (lead >= UTF8_FOUR_BYTE_BEGIN && lead <= UTF8_FOUR_BYTE_END) {
        scalar = lead & UTF8_FOUR_BYTE_PAYLOAD_MASK;
        minimum = FOUR_BYTE_SCALAR_BEGIN;
        return 3U;
    }
    throw std::runtime_error("Invalid UTF-8 leading byte in JSON string");
}
}  // 匿名命名空间

Json::Json(const char *value) : type(Type::STRING), string(RequireString(value))
{
}

void ValidateJsonString(const std::string &text)
{
    std::size_t position = 0U;
    while (position < text.size()) {
        const unsigned lead = static_cast<unsigned char>(text[position]);
        ++position;
        if (lead < ASCII_LIMIT) {
            continue;
        }
        unsigned scalar = 0U;
        unsigned minimum = 0U;
        const unsigned continuationCount = Utf8ContinuationCount(lead, scalar, minimum);
        if (continuationCount > (text.size() - position)) {
            throw std::runtime_error("Incomplete UTF-8 sequence in JSON string");
        }
        for (unsigned index = 0U; index < continuationCount; ++index) {
            const unsigned next = static_cast<unsigned char>(text[position]);
            ++position;
            if ((next & UTF8_CONTINUATION_MASK) != UTF8_CONTINUATION_TAG) {
                throw std::runtime_error("Invalid UTF-8 continuation byte in JSON string");
            }
            scalar = (scalar << UTF8_PAYLOAD_BITS) | (next & UTF8_PAYLOAD_MASK);
        }
        if (scalar < minimum || scalar > UNICODE_SCALAR_END || (scalar >= SURROGATE_BEGIN && scalar <= SURROGATE_END)) {
            throw std::runtime_error("Invalid Unicode scalar in JSON string");
        }
    }
}

Json Json::MakeObject() noexcept
{
    Json value;
    value.type = Type::OBJECT;
    return value;
}

Json Json::MakeArray() noexcept
{
    Json value;
    value.type = Type::ARRAY;
    return value;
}

Json &Json::operator[](const std::string &key)
{
    if (type != Type::OBJECT) {
        throw std::runtime_error("JSON value is not an object");
    }
    return object[key];
}

const Json *Json::Find(const std::string &key) const noexcept
{
    if (type != Type::OBJECT) {
        return nullptr;
    }
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}
}  // 命名空间 VnaWeb
