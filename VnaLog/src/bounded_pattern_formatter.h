/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_LOG_BOUNDED_PATTERN_FORMATTER_H
#define VNAPRO_VNA_LOG_BOUNDED_PATTERN_FORMATTER_H

#include <spdlog/pattern_formatter.h>

#include <cstddef>
#include <memory>
#include <string>

namespace VnaLog {
namespace Detail {

class BoundedPatternFormatter final : public spdlog::formatter {
public:
    // 使用原生 pattern，整条输出限制为 maxBytes；不足以保存截断标记时抛异常。
    BoundedPatternFormatter(std::string pattern, std::size_t maxBytes);
    // 整条记录包含前缀和换行，截断时回退到完整 UTF-8 字符边界。
    void format(const spdlog::details::log_msg &message, spdlog::memory_buf_t &destination) override;
    // 各 sink 克隆独立格式器状态，同时保留相同的 pattern 和大小约束。
    std::unique_ptr<spdlog::formatter> clone() const override;

private:
    std::string pattern_;
    std::size_t maxBytes_;
    spdlog::pattern_formatter formatter_;
};

}  // 命名空间 Detail
}  // 命名空间 VnaLog

#endif
