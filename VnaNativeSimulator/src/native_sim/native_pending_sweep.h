/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H
#define VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H

#include <cstdint>
#include <vector>

#include "vna_native/native_api_contract.h"

namespace vna_native {

// Owns the IQ frozen by one successful Start; later frame downloads cannot change it.
struct VNA_NATIVE_HIDDEN NativePendingSweep final {
    bool valid = false;
    std::uint32_t pointCount = 0U;
    std::uint32_t sourcePort = 0U;
    std::vector<double> iqData;

    // Replaces or consumes a pending result without allocation under the state mutex.
    void Swap(NativePendingSweep &other) noexcept;
};

}  // namespace vna_native
#endif  // VNAPRO_NATIVE_SIM_NATIVE_PENDING_SWEEP_H
