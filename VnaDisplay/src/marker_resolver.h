/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_MARKER_RESOLVER_H
#define VNAPRO_VNA_DISPLAY_MARKER_RESOLVER_H

#include "vna_display/trace_projection.h"

namespace VnaDisplay {
namespace Detail {

// 仅在同步标记解析期间借用同一完整帧的序列、频轴和格式投影，不保存到标记状态。
struct MarkerProjectionContext final {
    const VnaMeasurement::TraceResult &trace;
    const std::vector<double> &frequencyHz;
    const std::vector<FormattedPoint> &formattedPoints;
    DisplayFormat format = DisplayFormat::DB;
    std::uint64_t acquisitionRevision = 0U;
    bool reflectionInterpretation = false;
};

class MarkerResolver final {
public:
    MarkerResolver() = delete;
    static bool Apply(
        MarkerSettings &settings, const MarkerChange &change, const FrameContext &frame, std::string &error);
    static MarkerReadout Resolve(const MarkerSettings &settings, const MarkerProjectionContext &context);
    static bool ValidAxis(const std::vector<double> &frequencyHz) noexcept;
    static void Reconcile(
        MarkerSettings &settings, const std::vector<double> &frequencyHz, bool preserveSampleIndexes) noexcept;
};

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay

#endif
