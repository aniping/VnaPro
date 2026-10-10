/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H

#include <cstdint>
#include <vector>

#include "vna_native/native_api_contract.h"

namespace VnaNative {

// Borrowed ABI buffers. Lengths count elements; callers guarantee their actual capacity.
struct VNA_NATIVE_HIDDEN NormalFrameInput final {
    std::uint32_t pointCount = 0U;
    const std::uint32_t *parameters = nullptr;
    std::uint32_t parameterCount = 0U;
    const double *powerTable = nullptr;
    std::uint32_t powerValueCount = 0U;
};

// Raw driver units are preserved; no frequency or IF bandwidth conversion is implied.
struct VNA_NATIVE_HIDDEN NormalFramePoint final {
    std::uint64_t frequencyRaw = 0U;
    std::uint32_t sourcePort = 0U;
    std::uint32_t ifBandwidthRaw = 0U;
};

// Owns an independent copy of one source-port frame and its opaque power table.
struct VNA_NATIVE_HIDDEN NormalFrameSnapshot final {
    std::uint32_t sourcePort = 0U;
    std::vector<NormalFramePoint> points;
    std::vector<double> tableData;

    // Exchanges complete frames without allocation; callers provide synchronization.
    void Swap(NormalFrameSnapshot &other) noexcept;
};

}  // namespace VnaNative
#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_SNAPSHOT_H
