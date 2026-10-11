/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "uppercase_level_flag.h"

#include <array>
#include <string>

namespace VnaLog {
namespace Detail {

void UppercaseLevelFlag::format(
    const spdlog::details::log_msg &message, const std::tm &, spdlog::memory_buf_t &destination)
{
    static const std::array<std::string, 7U> names{{"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL", "OFF"}};
    const auto index = static_cast<std::size_t>(message.level);
    const auto &name = index < names.size() ? names[index] : names.back();
    destination.append(name.data(), name.data() + name.size());
}

std::unique_ptr<spdlog::custom_flag_formatter> UppercaseLevelFlag::clone() const
{
    return std::make_unique<UppercaseLevelFlag>();
}

}  // 命名空间 Detail
}  // 命名空间 VnaLog
