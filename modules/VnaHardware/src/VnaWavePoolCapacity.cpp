#include "VnaWavePoolCapacity.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "VnaCheckedSize.h"
#include "VnaDriverSdk/VnaDriverAbi.h"

namespace vna::detail {
namespace {

    VnaError poolError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    std::size_t maximumWaveCount(const std::vector<VnaWaveLayout> &layouts)
    {
        std::size_t result = 0;
        for (const auto &layout : layouts) {
            result = std::max(result, layout.waveKeys.size());
        }
        return result;
    }

    bool calculatePoolPayload(
        std::size_t waves, std::size_t points, std::size_t slots, std::size_t &valueCount, std::size_t &payloadBytes)
    {
        std::size_t valuesBytes = 0;
        std::size_t qualityBytes = 0;
        std::size_t slotBytes = 0;
        return checkedSizeMultiply(waves, points, valueCount) &&
               checkedSizeMultiply(valueCount, sizeof(VnaComplex64V1), valuesBytes) &&
               checkedSizeMultiply(valueCount, sizeof(std::uint32_t), qualityBytes) &&
               checkedSizeAdd(valuesBytes, qualityBytes, slotBytes) &&
               checkedSizeMultiply(slots, slotBytes, payloadBytes);
    }

}  // namespace

Result<std::size_t> inspectWavePoolPayload(const PreparedSweep &prepared, const VnaHardwareResourceLimits &limits)
{
    const std::size_t points = prepared.report.waveBlockPointCapacity;
    if (points == 0) {
        return Result<std::size_t>::failure(
            poolError(VnaErrorCode::ProtocolError, "driver batch capacity must be positive"));
    }
    std::size_t valueCount = 0;
    std::size_t payloadBytes = 0;
    if (!calculatePoolPayload(
            maximumWaveCount(prepared.layouts), points, limits.wavePoolSlotCount, valueCount, payloadBytes) ||
        (valueCount > std::vector<VnaComplex64V1>{}.max_size()) ||
        (valueCount > std::vector<std::uint32_t>{}.max_size())) {
        return Result<std::size_t>::failure(
            poolError(VnaErrorCode::ConfigurationRejected, "wave pool payload is not representable"));
    }
    if (payloadBytes > limits.maximumWavePoolPayloadBytes) {
        return Result<std::size_t>::failure(
            poolError(VnaErrorCode::ConfigurationRejected, "wave pool payload exceeds the deployment limit"));
    }
    return Result<std::size_t>::success(payloadBytes);
}

}  // namespace vna::detail
