/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "bounded_pattern_formatter.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "uppercase_level_flag.h"

namespace {

constexpr char TRUNCATION_MARKER[] = "...[truncated]\n";

std::size_t Utf8PrefixSize(const spdlog::memory_buf_t &text, std::size_t limit)
{
    while (limit > 0U && limit < text.size() && (static_cast<unsigned char>(text[limit]) & 0xC0U) == 0x80U) {
        --limit;
    }
    return limit;
}

}  // 匿名命名空间

namespace VnaLog {
namespace Detail {

BoundedPatternFormatter::BoundedPatternFormatter(std::string pattern, std::size_t maxBytes)
    : pattern_(std::move(pattern)), maxBytes_(maxBytes), formatter_("", spdlog::pattern_time_type::local, "\n")
{
    if (maxBytes_ < sizeof(TRUNCATION_MARKER) - 1U) {
        throw std::runtime_error("Log record limit is too small");
    }
    formatter_.add_flag<UppercaseLevelFlag>('K');
    formatter_.set_pattern(pattern_);
}

void BoundedPatternFormatter::format(const spdlog::details::log_msg &message, spdlog::memory_buf_t &destination)
{
    spdlog::memory_buf_t formatted;
    formatter_.format(message, formatted);
    if (formatted.size() <= maxBytes_) {
        destination.append(formatted.data(), formatted.data() + formatted.size());
        return;
    }
    const auto prefixSize = Utf8PrefixSize(formatted, maxBytes_ - (sizeof(TRUNCATION_MARKER) - 1U));
    destination.append(formatted.data(), formatted.data() + prefixSize);
    destination.append(TRUNCATION_MARKER, TRUNCATION_MARKER + sizeof(TRUNCATION_MARKER) - 1U);
    // 彩色输出目标使用格式器记录的区间；截断后必须同步收缩，防止越界读取。
    message.color_range_start = std::min(message.color_range_start, destination.size());
    message.color_range_end = std::min(message.color_range_end, destination.size());
}

std::unique_ptr<spdlog::formatter> BoundedPatternFormatter::clone() const
{
    return std::make_unique<BoundedPatternFormatter>(pattern_, maxBytes_);
}

}  // 命名空间 Detail
}  // 命名空间 VnaLog
