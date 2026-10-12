/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_MARKER_RESOLVER_H
#define VNAPRO_VNA_DISPLAY_MARKER_RESOLVER_H

#include "vna_display/trace_projection.h"

namespace VnaDisplay {
namespace Detail {

class MarkerResolver final {
public:
    MarkerResolver() = delete;
    static bool Apply(
        MarkerSettings &settings, const MarkerChange &change, const FrameContext &frame, std::string &error);
    static MarkerReadout Resolve(const MarkerSettings &settings, DisplayFormat format,
        const VnaMeasurement::TraceResult &trace, const std::vector<double> &frequencyHz,
        std::uint64_t acquisitionRevision);
    static bool ValidAxis(const std::vector<double> &frequencyHz) noexcept;
    static void Reconcile(
        MarkerSettings &settings, const std::vector<double> &frequencyHz, bool preserveSampleIndexes) noexcept;
};

}  // 命名空间 Detail
}  // 命名空间 VnaDisplay

#endif
