#ifndef VNA_DRIVER_SIMULATOR_SIMULATOR_WAVE_BLOCK_LEASE_H
#define VNA_DRIVER_SIMULATOR_SIMULATOR_WAVE_BLOCK_LEASE_H

#include <cstdint>

#include "VnaDriverSdk/VnaDriverApi.h"

namespace vna::simulator {

class SimulatorWaveBlockLease final {
public:
    SimulatorWaveBlockLease(const VnaDriverHostApiV1 &hostApi, VnaBufferHandle handle) noexcept;
    ~SimulatorWaveBlockLease() noexcept;
    SimulatorWaveBlockLease(const SimulatorWaveBlockLease &) = delete;
    SimulatorWaveBlockLease &operator=(const SimulatorWaveBlockLease &) = delete;
    SimulatorWaveBlockLease(SimulatorWaveBlockLease &&) = delete;
    SimulatorWaveBlockLease &operator=(SimulatorWaveBlockLease &&) = delete;

    VnaStatusV1 commit(std::uint32_t firstPoint, std::uint32_t pointCount);

private:
    // 租约短于所属 SimulatorDevice，因此可以借用其按值保存且地址稳定的 Host API 表。
    const VnaDriverHostApiV1 &hostApi_;
    VnaBufferHandle handle_{nullptr};
    bool active_{true};
};

}  // namespace vna::simulator

#endif
