#include "SignalModel.h"

#include <cstddef>
#include <limits>

namespace vna_native {
namespace {

double FrequencyFactor(std::uint64_t frequencyRaw)
{
    std::uint64_t mixed = frequencyRaw;
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xff51afd7ed558ccd);
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xc4ceb9fe1a85ec53);
    mixed ^= mixed >> 33U;
    return static_cast<double>(mixed >> 11U) / 9007199254740992.0;
}

bool CheckedOutputValueCount(std::size_t pointCount, std::size_t* valueCount)
{
    if (valueCount == NULL || pointCount == 0U) {
        return false;
    }
    const std::size_t maxSize = (std::numeric_limits<std::size_t>::max)();
    if (pointCount > maxSize / static_cast<std::size_t>(kIqValuesPerPoint)) {
        return false;
    }
    const std::size_t count = pointCount * static_cast<std::size_t>(kIqValuesPerPoint);
    if (count > maxSize / sizeof(double)) {
        return false;
    }
    *valueCount = count;
    return true;
}

}  // namespace

std::uint32_t SignalModel::Generate(const NormalFrameSnapshot& frame, std::vector<double>* output)
{
    if (output == NULL || frame.points.empty()) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (frame.sourcePort >= kNativePortCount) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
        if (frame.points[pointIndex].sourcePort != frame.sourcePort) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
    }

    std::size_t outputValueCount = 0U;
    if (!CheckedOutputValueCount(frame.points.size(), &outputValueCount)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    try {
        std::vector<double> candidate(outputValueCount, 0.0);
        const double sourceLabel = static_cast<double>(frame.sourcePort + 1U);

        for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
            const double frequencyFactor = FrequencyFactor(frame.points[pointIndex].frequencyRaw);
            const double sourceAReal = 1.0 + 0.1 * sourceLabel + 0.01 * sourceLabel;
            const double sourceAImag = 0.001 * frequencyFactor;
            const std::size_t pointBase = pointIndex * static_cast<std::size_t>(kIqValuesPerPoint);

            for (std::uint32_t receiverIndex = 0U; receiverIndex < kNativePortCount; ++receiverIndex) {
                const double receiverLabel = static_cast<double>(receiverIndex + 1U);
                const double aReal = 1.0 + 0.1 * receiverLabel + 0.01 * sourceLabel;
                const double aImag = 0.001 * frequencyFactor;
                const double hReal = (receiverIndex == frame.sourcePort ? 0.20 : 0.02 * receiverLabel * sourceLabel) +
                                     0.001 * frequencyFactor;
                const double hImag = 0.005 * (receiverLabel - sourceLabel);
                const double bReal = hReal * sourceAReal - hImag * sourceAImag;
                const double bImag = hReal * sourceAImag + hImag * sourceAReal;

                const std::size_t receiverBase = pointBase + static_cast<std::size_t>(receiverIndex) * 4U;
                candidate[receiverBase] = aReal;
                candidate[receiverBase + 1U] = aImag;
                candidate[receiverBase + 2U] = bReal;
                candidate[receiverBase + 3U] = bImag;
            }
        }

        output->swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

}  // namespace vna_native
