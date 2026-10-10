/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_MEASUREMENT_FRAME_H
#define VNAPRO_VNA_CORE_MEASUREMENT_FRAME_H

#include <cstdint>
#include <memory>
#include <vector>

#include "vna_core/port_frame.h"
#include "vna_core/sweep_plan.h"

namespace VnaCore {

struct MeasurementFrame final {
    std::uint32_t channelId = 1U;
    std::uint64_t runId = 0U;
    std::uint64_t configRevision = 0U;
    std::uint64_t frameSeq = 0U;
    SweepPlan plan;
    std::vector<PortFrame> ports;
};

using FramePtr = std::shared_ptr<const MeasurementFrame>;

}  // namespace VnaCore

#endif
