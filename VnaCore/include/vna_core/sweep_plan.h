/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_SWEEP_PLAN_H
#define VNAPRO_VNA_CORE_SWEEP_PLAN_H

#include <cstdint>
#include <vector>

namespace vna_core {

struct SweepPlan final {
    std::vector<std::uint64_t> frequencyRaw;
    std::vector<double> frequencyHz;
    std::uint32_t ifBandwidthRaw = 0U;
    std::uint32_t receiverMask = 0U;
    std::vector<std::uint32_t> sourcePorts;  // Physical ports 1..4.
    std::vector<double> tableData;           // Owned, opaque 9N table values.
};

}  // namespace vna_core

#endif
