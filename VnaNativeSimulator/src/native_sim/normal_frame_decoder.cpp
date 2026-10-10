/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "normal_frame_decoder.h"

#include <cstddef>
#include <limits>

namespace vna_native {
namespace {

constexpr std::size_t PARAM_HEADER_VALUES = 1U;
constexpr std::size_t FREQUENCY_LOW_OFFSET = 1U;
constexpr std::size_t SOURCE_PORT_OFFSET = 2U;
constexpr std::size_t DYNAMIC_LINK_OFFSET = 3U;
constexpr std::size_t IF_BANDWIDTH_OFFSET = 4U;
constexpr std::size_t DYNAMIC_ENABLED_OFFSET = 5U;
constexpr unsigned FREQUENCY_WORD_BITS = 32U;

bool CheckedElementBytes(std::uint64_t elementCount, std::size_t elementSize) noexcept
{
    const std::uint64_t maxSize = (std::numeric_limits<std::size_t>::max)();
    return elementSize != 0U && elementCount <= (maxSize / elementSize);
}

std::size_t PointParameterBase(std::uint32_t pointIndex) noexcept
{
    return PARAM_HEADER_VALUES + (std::size_t{PARAM_VALUES_PER_POINT} * pointIndex);
}

std::uint32_t ValidateFields(const NormalFrameInput &input) noexcept
{
    const std::uint32_t firstSourcePort = input.parameters[PARAM_HEADER_VALUES + SOURCE_PORT_OFFSET];
    if (firstSourcePort >= NATIVE_PORT_COUNT) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    // Reject invalid or mixed ports before inspecting unsupported flags, preserving error precedence.
    for (std::uint32_t pointIndex = 1U; pointIndex < input.pointCount; ++pointIndex) {
        const std::uint32_t sourcePort = input.parameters[PointParameterBase(pointIndex) + SOURCE_PORT_OFFSET];
        if (sourcePort >= NATIVE_PORT_COUNT || sourcePort != firstSourcePort) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
    }
    if (input.parameters[0U] != 0U) {
        return DRV_SIM_NOT_SUPPORTED;
    }
    for (std::uint32_t pointIndex = 0U; pointIndex < input.pointCount; ++pointIndex) {
        const std::size_t base = PointParameterBase(pointIndex);
        if (input.parameters[base + DYNAMIC_LINK_OFFSET] != 0U ||
            input.parameters[base + DYNAMIC_ENABLED_OFFSET] != 0U) {
            return DRV_SIM_NOT_SUPPORTED;
        }
    }
    return DRV_SIM_OK;
}

NormalFrameSnapshot CopyFrame(const NormalFrameInput &input)
{
    NormalFrameSnapshot candidate;
    candidate.sourcePort = input.parameters[PARAM_HEADER_VALUES + SOURCE_PORT_OFFSET];
    candidate.points.resize(input.pointCount);
    for (std::uint32_t pointIndex = 0U; pointIndex < input.pointCount; ++pointIndex) {
        const std::size_t base = PointParameterBase(pointIndex);
        NormalFramePoint &point = candidate.points[pointIndex];
        point.frequencyRaw = (std::uint64_t{input.parameters[base]} << FREQUENCY_WORD_BITS) |
                             std::uint64_t{input.parameters[base + FREQUENCY_LOW_OFFSET]};
        point.sourcePort = input.parameters[base + SOURCE_PORT_OFFSET];
        point.ifBandwidthRaw = input.parameters[base + IF_BANDWIDTH_OFFSET];
    }
    // The power table is opaque; non-finite values are preserved rather than interpreted.
    candidate.tableData.assign(input.powerTable, input.powerTable + input.powerValueCount);
    return candidate;
}

}  // namespace

namespace NormalFrameDecoder {

std::uint32_t ValidateInputShape(const NormalFrameInput &input) noexcept
{
    if (input.parameters == nullptr || input.powerTable == nullptr || input.pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    const std::uint64_t points{input.pointCount};
    const std::uint64_t expectedParamCount = PARAM_HEADER_VALUES + (std::uint64_t{PARAM_VALUES_PER_POINT} * points);
    const std::uint64_t expectedTableCount = std::uint64_t{TABLE_VALUES_PER_POINT} * points;
    if (expectedParamCount > (std::numeric_limits<std::uint32_t>::max)() ||
        expectedTableCount > (std::numeric_limits<std::uint32_t>::max)() ||
        !CheckedElementBytes(expectedParamCount, sizeof(std::uint32_t)) ||
        !CheckedElementBytes(expectedTableCount, sizeof(double)) ||
        !CheckedElementBytes(points, sizeof(NormalFramePoint))) {
        return DRV_SIM_SIZE_OVERFLOW;
    }
    if (input.parameterCount != expectedParamCount || input.powerValueCount != expectedTableCount) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    return DRV_SIM_OK;
}

std::uint32_t Decode(const NormalFrameInput &input, NormalFrameSnapshot &output) noexcept
{
    const std::uint32_t shapeStatus = ValidateInputShape(input);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }
    const std::uint32_t fieldStatus = ValidateFields(input);
    if (fieldStatus != DRV_SIM_OK) {
        return fieldStatus;
    }
    try {
        NormalFrameSnapshot candidate = CopyFrame(input);
        output.Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        // Convert allocation failures to the driver status; the uncommitted output is retained.
        return DRV_SIM_INTERNAL_ERROR;
    }
}

}  // namespace NormalFrameDecoder
}  // namespace vna_native
