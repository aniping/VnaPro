/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_SWEEP_PLAN_H
#define VNAPRO_VNA_CORE_SWEEP_PLAN_H

#include <cstdint>
#include <vector>

namespace VnaCore {

struct SweepPlan final {
    std::vector<std::uint64_t> frequencyRaw;
    std::vector<double> frequencyHz;
    std::uint32_t ifBandwidthRaw = 0U;
    std::uint32_t receiverMask = 0U;
    std::vector<std::uint32_t> sourcePorts;  // 物理端口编号为 1～4。
    std::vector<double> tableData;           // 独立持有的 9N 个表值，其内容不在此处解释。
};

}  // 命名空间 VnaCore

#endif
