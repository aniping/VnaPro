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
    std::vector<std::uint64_t> powerRaw;  // 新接口的逐点原始功率值；空数组表示使用模拟器默认值 0。
    std::uint32_t ifBandwidthRaw = 0U;
    std::uint32_t receiverMask = 0U;
    std::vector<std::uint32_t> sourcePorts;  // 物理端口编号为 1～4。
    std::vector<double> tableData;  // 旧 VN08 接口需要的 9N 个不透明表值；新 VN10 接口可为空。
};

}  // 命名空间 VnaCore

#endif
