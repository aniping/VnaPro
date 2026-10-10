/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H

#include <cstdint>
#include <vector>

#include "vna_native/native_api_contract.h"

namespace VnaNative {

// 借用的 ABI 缓冲区；长度以元素计数，调用者保证实际容量。
struct VNA_NATIVE_HIDDEN NormalFrameInput final {
    std::uint32_t pointCount = 0U;
    const std::uint32_t *parameters = nullptr;
    std::uint32_t parameterCount = 0U;
    const double *powerTable = nullptr;
    std::uint32_t powerValueCount = 0U;
};

// 保留驱动的原始单位，不隐含频率或中频带宽换算。
struct VNA_NATIVE_HIDDEN NormalFramePoint final {
    std::uint64_t frequencyRaw = 0U;
    std::uint32_t sourcePort = 0U;
    std::uint32_t ifBandwidthRaw = 0U;
};

// 独立持有一个源端口帧及其功率表副本，不解释功率表内容。
struct VNA_NATIVE_HIDDEN NormalFrameSnapshot final {
    std::uint32_t sourcePort = 0U;
    std::vector<NormalFramePoint> points;
    std::vector<double> tableData;

    // 交换完整帧而不进行内存分配；同步由调用者保证。
    void Swap(NormalFrameSnapshot &other) noexcept;
};

}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H
