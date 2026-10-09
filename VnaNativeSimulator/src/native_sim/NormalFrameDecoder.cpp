#include "NormalFrameDecoder.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace vna_native {
namespace {

bool CheckedElementBytes(std::uint64_t elementCount, std::size_t elementSize)
{
    if (elementSize == 0U) {
        return false;
    }
    const std::uint64_t maxSize = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
    return elementCount <= maxSize / elementSize;
}

}  // namespace

NormalFramePoint::NormalFramePoint() : frequencyRaw(0U), sourcePort(0U), ifBandwidthRaw(0U) {}

NormalFrameSnapshot::NormalFrameSnapshot() : sourcePort(0U) {}

void NormalFrameSnapshot::Swap(NormalFrameSnapshot& other) noexcept
{
    using std::swap;
    swap(sourcePort, other.sourcePort);
    points.swap(other.points);
    tableData.swap(other.tableData);
}

std::uint32_t NormalFrameDecoder::ValidateInputShape(std::uint32_t pointCount, const std::uint32_t* paramData,
    std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen)
{
    if (paramData == NULL || tableData == NULL) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    const std::uint64_t points = static_cast<std::uint64_t>(pointCount);
    const std::uint64_t expectedParamCount = 1U + static_cast<std::uint64_t>(kParamValuesPerPoint) * points;
    const std::uint64_t expectedTableCount = static_cast<std::uint64_t>(kTableValuesPerPoint) * points;

    if (expectedParamCount > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) ||
        expectedTableCount > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) ||
        !CheckedElementBytes(expectedParamCount, sizeof(std::uint32_t)) ||
        !CheckedElementBytes(expectedTableCount, sizeof(double)) ||
        !CheckedElementBytes(points, sizeof(NormalFramePoint))) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    if (paramDataLen != static_cast<std::uint32_t>(expectedParamCount) ||
        tableDataLen != static_cast<std::uint32_t>(expectedTableCount)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    return DRV_SIM_OK;
}

std::uint32_t NormalFrameDecoder::Decode(std::uint32_t pointCount, const std::uint32_t* paramData,
    std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen, NormalFrameSnapshot* output)
{
    if (output == NULL) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    const std::uint32_t shapeStatus = ValidateInputShape(pointCount, paramData, paramDataLen, tableData, tableDataLen);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        // 结构和值域错误优先于“不支持”字段，保证复合错误结果稳定。
        const std::uint32_t firstSourcePort = paramData[3U];
        if (firstSourcePort >= kNativePortCount) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
        for (std::uint32_t pointIndex = 1U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            const std::uint32_t sourcePort = paramData[base + 2U];
            if (sourcePort >= kNativePortCount || sourcePort != firstSourcePort) {
                return DRV_SIM_INVALID_ARGUMENT;
            }
        }

        if (paramData[0U] != 0U) {
            return DRV_SIM_NOT_SUPPORTED;
        }
        for (std::uint32_t pointIndex = 0U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            if (paramData[base + 3U] != 0U || paramData[base + 5U] != 0U) {
                return DRV_SIM_NOT_SUPPORTED;
            }
        }

        NormalFrameSnapshot candidate;
        candidate.sourcePort = firstSourcePort;
        candidate.points.resize(static_cast<std::size_t>(pointCount));

        for (std::uint32_t pointIndex = 0U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            NormalFramePoint& point = candidate.points[static_cast<std::size_t>(pointIndex)];
            point.frequencyRaw =
                (static_cast<std::uint64_t>(paramData[base]) << 32U) | static_cast<std::uint64_t>(paramData[base + 1U]);
            point.sourcePort = paramData[base + 2U];
            point.ifBandwidthRaw = paramData[base + 4U];
        }

        candidate.tableData.assign(tableData, tableData + static_cast<std::size_t>(tableDataLen));
        output->Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

}  // namespace vna_native
