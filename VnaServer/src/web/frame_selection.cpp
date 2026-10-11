/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_selection.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <stdexcept>

namespace VnaWeb {
namespace {
constexpr std::size_t DISPLAY_TRACE_LIMIT = 4U;
constexpr std::size_t MAX_MEASUREMENT_NAME_LENGTH = 128U;
constexpr unsigned HEX_RADIX = 16U;
constexpr unsigned HEX_ALPHA_OFFSET = 10U;
constexpr unsigned ASCII_CONTROL_LIMIT = 32U;
constexpr unsigned ASCII_DELETE = 127U;
constexpr char TRACE_QUERY_PREFIX[] = "trace=";

unsigned HexDigit(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<unsigned>(value - 'a' + HEX_ALPHA_OFFSET);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<unsigned>(value - 'A' + HEX_ALPHA_OFFSET);
    }
    throw std::invalid_argument("Invalid query escape");
}

std::string DecodeValue(const std::string &raw)
{
    std::string value;
    for (std::size_t index = 0U; index < raw.size(); ++index) {
        char character = raw[index];
        if (character == '%') {
            if (index + 2U >= raw.size()) {
                throw std::invalid_argument("Incomplete query escape");
            }
            character = static_cast<char>(HexDigit(raw[index + 1U]) * HEX_RADIX + HexDigit(raw[index + 2U]));
            index += 2U;
        } else if (character == '+') {
            character = ' ';
        }
        if (static_cast<unsigned char>(character) < ASCII_CONTROL_LIMIT ||
            static_cast<unsigned char>(character) == ASCII_DELETE) {
            throw std::invalid_argument("Control character in trace name");
        }
        value.push_back(character);
    }
    if (value.empty() || value.size() > MAX_MEASUREMENT_NAME_LENGTH) {
        throw std::invalid_argument("Trace name must contain 1..128 bytes");
    }
    return value;
}
}  // 匿名命名空间

std::vector<VnaService::Measurement> SelectFrameMeasurements(
    const std::string &query, const std::vector<VnaService::Measurement> &catalog)
{
    if (query.empty()) {
        return {catalog.begin(), catalog.begin() + (std::min)(catalog.size(), DISPLAY_TRACE_LIMIT)};
    }
    if (query == "display=none") {
        return {};
    }
    std::vector<VnaService::Measurement> selected;
    std::set<std::string> names;
    for (std::size_t begin = 0U; begin < query.size();) {
        const std::size_t end = query.find('&', begin);
        const std::string field = query.substr(begin, end == std::string::npos ? end : end - begin);
        if (field.find(TRACE_QUERY_PREFIX) != 0U || selected.size() == DISPLAY_TRACE_LIMIT) {
            throw std::invalid_argument("Expected at most four trace query fields");
        }
        const std::string name = DecodeValue(field.substr(sizeof(TRACE_QUERY_PREFIX) - 1U));
        const auto found = std::find_if(catalog.begin(), catalog.end(), [&name](const VnaService::Measurement &item) {
            return item.name == name;
        });
        if (found == catalog.end() || !names.insert(name).second) {
            throw std::invalid_argument("Unknown or duplicate trace name");
        }
        selected.push_back(*found);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
        if (begin == query.size()) {
            throw std::invalid_argument("Empty query field");
        }
    }
    return selected;
}

}  // 命名空间 VnaWeb
