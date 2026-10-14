/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/json_encoder.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace VnaWeb {
namespace {
constexpr std::size_t OUTPUT_LIMIT = 256U * 1024U * 1024U;
constexpr unsigned CONTROL_CHARACTER_LIMIT = 32U;
constexpr unsigned HEX_NIBBLE_BITS = 4U;
constexpr unsigned HEX_NIBBLE_MASK = 15U;
constexpr unsigned JSON_NUMBER_PRECISION = 17U;
constexpr std::size_t CONTROL_ESCAPE_LENGTH = 6U;

void AppendBounded(std::string &output, const char *text, std::size_t length)
{
    if (output.size() > OUTPUT_LIMIT || length > OUTPUT_LIMIT - output.size()) {
        throw std::runtime_error("JSON output exceeds 256 MiB");
    }
    output.append(text, length);
}

void AppendLiteral(std::string &output, const char *text)
{
    AppendBounded(output, text, std::strlen(text));
}

void AppendCharacter(std::string &output, char character)
{
    AppendBounded(output, &character, 1U);
}

void AppendEscapedJsonString(std::string &output, const std::string &text)
{
    ValidateJsonString(text);
    AppendCharacter(output, '"');
    const char hex[] = "0123456789abcdef";
    for (unsigned char character : text) {
        if (character == '"' || character == '\\') {
            AppendCharacter(output, '\\');
            AppendCharacter(output, static_cast<char>(character));
        } else if (character < CONTROL_CHARACTER_LIMIT) {
            const char escaped[CONTROL_ESCAPE_LENGTH] = {
                '\\', 'u', '0', '0', hex[character >> HEX_NIBBLE_BITS], hex[character & HEX_NIBBLE_MASK]};
            AppendBounded(output, escaped, sizeof(escaped));
        } else {
            AppendCharacter(output, static_cast<char>(character));
        }
    }
    AppendCharacter(output, '"');
}
}  // 匿名命名空间

JsonEncoder::JsonEncoder(Json value)
{
    Reset(std::move(value));
}

void JsonEncoder::Reset(Json value)
{
    state_ = State::FAILED;
    stack_.clear();
    output_.clear();
    value_ = std::move(value);
    stack_.emplace_back(value_);
    state_ = State::ACTIVE;
}

bool JsonEncoder::Advance(unsigned budget)
{
    if (state_ == State::COMPLETE) {
        return true;
    }
    if (state_ != State::ACTIVE) {
        throw std::logic_error("JSON encoder requires Reset before encoding");
    }
    try {
        for (unsigned count = 0U; count < budget && !stack_.empty(); ++count) {
            ProcessTop();
        }
    } catch (...) {
        stack_.clear();
        output_.clear();
        state_ = State::FAILED;
        throw;
    }
    if (stack_.empty()) {
        state_ = State::COMPLETE;
    }
    return state_ == State::COMPLETE;
}

void JsonEncoder::ProcessTop()
{
    JsonEncodingFrame &frame = stack_.back();
    const Json &value = frame.value.get();
    if (value.type == Json::Type::ARRAY || value.type == Json::Type::OBJECT) {
        ProcessContainer(frame);
    } else {
        AppendScalar(value);
        stack_.pop_back();
    }
}

void JsonEncoder::ProcessContainer(JsonEncodingFrame &frame)
{
    const Json &value = frame.value.get();
    const bool array = value.type == Json::Type::ARRAY;
    if (!frame.opened) {
        AppendCharacter(output_, array ? '[' : '{');
        frame.opened = true;
    } else if ((array && frame.index == value.array.size()) || (!array && frame.member == value.object.end())) {
        AppendCharacter(output_, array ? ']' : '}');
        stack_.pop_back();
    } else {
        AppendChild(frame);
    }
}

void JsonEncoder::AppendChild(JsonEncodingFrame &frame)
{
    const Json &value = frame.value.get();
    if (frame.index != 0U) {
        AppendCharacter(output_, ',');
    }
    const Json *child = nullptr;
    if (value.type == Json::Type::ARRAY) {
        child = &value.array[frame.index];
    } else {
        AppendEscapedJsonString(output_, frame.member->first);
        AppendCharacter(output_, ':');
        child = &frame.member->second;
        ++frame.member;
    }
    ++frame.index;
    stack_.emplace_back(*child);
}

void JsonEncoder::AppendScalar(const Json &value)
{
    if (value.type == Json::Type::NULL_VALUE) {
        AppendLiteral(output_, "null");
    } else if (value.type == Json::Type::BOOLEAN) {
        AppendLiteral(output_, value.boolean ? "true" : "false");
    } else if (value.type == Json::Type::STRING) {
        AppendEscapedJsonString(output_, value.string);
    } else if (value.type != Json::Type::NUMBER) {
        throw std::runtime_error("Invalid JSON value type");
    } else if (!std::isfinite(value.number)) {
        AppendLiteral(output_, "null");
    } else {
        std::ostringstream number;
        number.exceptions(std::ios::badbit | std::ios::failbit);
        number.imbue(std::locale::classic());
        number << std::setprecision(JSON_NUMBER_PRECISION) << value.number;
        const std::string text = number.str();
        AppendBounded(output_, text.data(), text.size());
    }
}

std::string JsonEncoder::Take()
{
    if (state_ != State::COMPLETE) {
        throw std::logic_error("JSON output is not complete or was already taken");
    }
    state_ = State::TAKEN;
    return std::move(output_);
}

std::string StringifyJson(const Json &value)
{
    JsonEncoder encoder(value);
    while (!encoder.Advance()) {
    }
    return encoder.Take();
}
}  // 命名空间 VnaWeb
