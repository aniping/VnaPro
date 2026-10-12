/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_TRACE_SEQUENCE_FORMATTER_H
#define VNAPRO_VNA_DISPLAY_TRACE_SEQUENCE_FORMATTER_H

#include "vna_display/trace_projection.h"

namespace VnaDisplay {
namespace Detail {

// 连续相位和频率导数必须先处理完整序列，不能逐个孤立点转换。
class TraceSequenceFormatter final {
public:
    TraceSequenceFormatter() = delete;
    static bool RequiresSequence(DisplayFormat format) noexcept;
    static std::vector<FormattedPoint> Calculate(
        const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz, DisplayFormat format);
};

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay

#endif
