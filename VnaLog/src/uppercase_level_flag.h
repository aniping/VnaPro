/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_LOG_UPPERCASE_LEVEL_FLAG_H
#define VNAPRO_VNA_LOG_UPPERCASE_LEVEL_FLAG_H

#include <spdlog/pattern_formatter.h>

#include <memory>

namespace VnaLog {
namespace Detail {

class UppercaseLevelFlag final : public spdlog::custom_flag_formatter {
public:
    // 方法名遵循第三方虚函数接口，输出等级名称不修改第三方库全局配置。
    void format(
        const spdlog::details::log_msg &message, const std::tm &time, spdlog::memory_buf_t &destination) override;
    // 为原生 pattern_formatter 创建独立的大写等级标志实例。
    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override;
};

}  // 命名空间 Detail
}  // 命名空间 VnaLog

#endif
