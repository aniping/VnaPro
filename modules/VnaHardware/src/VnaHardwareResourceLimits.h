#ifndef VNA_HARDWARE_VNA_HARDWARE_RESOURCE_LIMITS_H
#define VNA_HARDWARE_VNA_HARDWARE_RESOURCE_LIMITS_H

#include <cstddef>
#include <string>

#include "Result.h"

namespace vna::detail {

struct TrustedConfigurationFile;

constexpr std::size_t maximumWavePoolSlotCount = 64;

struct VnaHardwareResourceLimits {
    std::size_t wavePoolSlotCount{2};
    std::size_t maximumWavePoolPayloadBytes{0};
    std::size_t maximumRequestedPlanPayloadBytes{0};
    std::size_t maximumPreparedSweepPayloadBytes{0};
    std::size_t maximumPendingCommandsPerDevice{0};
    std::size_t maximumPendingPreparePayloadBytesPerDevice{0};
};

Result<VnaHardwareResourceLimits> loadVnaHardwareResourceLimits(const std::string &configurationFile);
Result<VnaHardwareResourceLimits> loadVnaHardwareResourceLimits(const TrustedConfigurationFile &configurationFile);

}  // namespace vna::detail

#endif
