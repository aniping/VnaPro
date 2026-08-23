#ifndef VNA_HARDWARE_VNA_WAVE_POOL_CAPACITY_H
#define VNA_HARDWARE_VNA_WAVE_POOL_CAPACITY_H

#include <cstddef>

#include "Result.h"
#include "VnaHardwareResourceLimits.h"
#include "VnaSweepPlan.h"

namespace vna::detail {

Result<std::size_t> inspectWavePoolPayload(const PreparedSweep &prepared, const VnaHardwareResourceLimits &limits);

}  // namespace vna::detail

#endif
