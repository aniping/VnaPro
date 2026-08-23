#include "AcquisitionCapacity.h"

#include <limits>

namespace vna::detail {
namespace {

    Result<std::size_t> checkedRawFramePayload(const AcquisitionShape &shape)
    {
        if ((shape.waveSampleCount > std::vector<std::complex<double>>().max_size()) ||
            (shape.waveSampleCount > std::vector<std::uint32_t>().max_size()) ||
            (shape.layoutCount > std::vector<std::size_t>().max_size())) {
            return Result<std::size_t>::failure(
                acquisitionError(VnaErrorCode::UnsupportedPlan, "raw frame element count exceeds max_size"));
        }
        std::size_t valuesBytes = 0;
        std::size_t qualityBytes = 0;
        std::size_t progressBytes = 0;
        std::size_t payloadBytes = 0;
        if (!checkedMultiply(shape.waveSampleCount, sizeof(std::complex<double>), valuesBytes) ||
            !checkedMultiply(shape.waveSampleCount, sizeof(std::uint32_t), qualityBytes) ||
            !checkedMultiply(shape.layoutCount, sizeof(std::size_t), progressBytes) ||
            !checkedAdd(valuesBytes, qualityBytes, payloadBytes) || !checkedAccumulate(progressBytes, payloadBytes)) {
            return Result<std::size_t>::failure(
                acquisitionError(VnaErrorCode::UnsupportedPlan, "raw frame payload size overflows"));
        }
        return Result<std::size_t>::success(payloadBytes);
    }

    Result<std::size_t> checkedPreparedMappingPayload(const AcquisitionShape &shape)
    {
        if ((shape.pointExcitationRecordCount > std::vector<std::uint32_t>().max_size()) ||
            (shape.layoutCount > std::vector<PreparedLayoutMapping>().max_size()) ||
            (shape.waveMappingCount > std::vector<PreparedWaveMapping>().max_size())) {
            return Result<std::size_t>::failure(
                acquisitionError(VnaErrorCode::UnsupportedPlan, "prepared mapping element count exceeds max_size"));
        }
        std::size_t pointBytes = 0;
        std::size_t layoutBytes = 0;
        std::size_t waveBytes = 0;
        std::size_t payloadBytes = 0;
        if (!checkedMultiply(shape.pointExcitationRecordCount, sizeof(std::uint32_t), pointBytes) ||
            !checkedMultiply(shape.layoutCount, sizeof(PreparedLayoutMapping), layoutBytes) ||
            !checkedMultiply(shape.waveMappingCount, sizeof(PreparedWaveMapping), waveBytes) ||
            !checkedAdd(pointBytes, layoutBytes, payloadBytes) || !checkedAccumulate(waveBytes, payloadBytes)) {
            return Result<std::size_t>::failure(
                acquisitionError(VnaErrorCode::UnsupportedPlan, "prepared mapping payload size overflows"));
        }
        return Result<std::size_t>::success(payloadBytes);
    }

}  // namespace

bool checkedAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max() - left)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if ((left != 0) && (right > (std::numeric_limits<std::size_t>::max() / left))) {
        return false;
    }
    result = left * right;
    return true;
}

bool checkedAccumulate(std::size_t value, std::size_t &total) noexcept
{
    std::size_t result = 0;
    if (!checkedAdd(total, value, result)) {
        return false;
    }
    total = result;
    return true;
}

VnaError acquisitionError(VnaErrorCode code, const char *description)
{
    VnaError error;
    error.code = code;
    error.description = description;
    return error;
}

Result<AcquisitionPlanInspection> inspectAcquisitionShape(
    const AcquisitionShape &shape, const AcquisitionLimits &limits)
{
    auto rawPayload = checkedRawFramePayload(shape);
    if (!rawPayload.hasValue()) {
        return Result<AcquisitionPlanInspection>::failure(rawPayload.error());
    }
    auto mappingPayload = checkedPreparedMappingPayload(shape);
    if (!mappingPayload.hasValue()) {
        return Result<AcquisitionPlanInspection>::failure(mappingPayload.error());
    }
    if ((rawPayload.value() > limits.maximumRawFramePayloadBytes) ||
        (mappingPayload.value() > limits.maximumPreparedMappingPayloadBytes)) {
        return Result<AcquisitionPlanInspection>::failure(
            acquisitionError(VnaErrorCode::UnsupportedPlan, "acquisition payload exceeds configured limits"));
    }
    AcquisitionPlanInspection inspection;
    inspection.pointExcitationRecordCount = shape.pointExcitationRecordCount;
    inspection.waveSampleCount = shape.waveSampleCount;
    inspection.rawFramePayloadBytes = rawPayload.value();
    inspection.preparedMappingPayloadBytes = mappingPayload.value();
    return Result<AcquisitionPlanInspection>::success(inspection);
}

}  // namespace vna::detail
