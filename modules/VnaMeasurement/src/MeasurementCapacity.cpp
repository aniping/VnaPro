#include "MeasurementInternal.h"

#include <limits>
#include <type_traits>
#include <utility>

namespace vna::detail {
namespace {

    static_assert(std::is_standard_layout<FlatPlanHeader>::value, "FlatPlanHeader must have standard layout");
    static_assert(std::is_standard_layout<FlatPointSignature>::value, "FlatPointSignature must have standard layout");
    static_assert(
        std::is_standard_layout<FlatExcitationSignature>::value, "FlatExcitationSignature must have standard layout");
    static_assert(std::is_standard_layout<FlatSourceSignature>::value, "FlatSourceSignature must have standard layout");
    static_assert(std::is_standard_layout<CompiledDependency>::value, "CompiledDependency must have standard layout");
    static_assert(
        std::is_standard_layout<PreparedExcitationMapping>::value,
        "PreparedExcitationMapping must have standard layout");
    static_assert(std::is_standard_layout<BoundDependency>::value, "BoundDependency must have standard layout");
    static_assert(std::is_standard_layout<BoundObservation>::value, "BoundObservation must have standard layout");

    template <typename T>
    bool countFits(std::size_t count)
    {
        return count <= std::vector<T>().max_size();
    }

    bool compiledContainersCanHold(const MeasurementShape &shape)
    {
        return countFits<FlatPointSignature>(shape.pointCount) &&
               countFits<FlatExcitationSignature>(shape.excitationOccurrenceCount) &&
               countFits<FlatSourceSignature>(shape.sourceCount) &&
               countFits<ObservationDefinition>(shape.observationCount) &&
               countFits<CompiledDependency>(shape.dependencyCount);
    }

    bool boundContainersCanHold(const MeasurementShape &shape)
    {
        return countFits<PreparedExcitationMapping>(shape.excitationCount) &&
               countFits<std::uint32_t>(shape.pointIndexCount) && countFits<BoundDependency>(shape.dependencyCount) &&
               countFits<BoundObservation>(shape.observationCount);
    }

    bool buildContainersCanHold(const MeasurementShape &shape)
    {
        return countFits<std::complex<double>>(shape.observationSampleCount) &&
               countFits<ObservationSampleStatus>(shape.observationSampleCount) &&
               countFits<RawWaveSeriesView>(shape.dependencyCount);
    }

    bool payloadContainersCanHold(const MeasurementShape &shape)
    {
        return compiledContainersCanHold(shape) && boundContainersCanHold(shape) && buildContainersCanHold(shape);
    }

    bool addArrayPayload(std::size_t count, std::size_t elementSize, std::size_t &total) noexcept
    {
        std::size_t bytes = 0;
        return checkedMeasurementMultiply(count, elementSize, bytes) && checkedMeasurementAccumulate(bytes, total);
    }

    bool checkedCompiledPayload(const MeasurementShape &shape, std::size_t &bytes) noexcept
    {
        bytes = sizeof(FlatPlanHeader);
        return addArrayPayload(shape.pointCount, sizeof(FlatPointSignature), bytes) &&
               addArrayPayload(shape.excitationOccurrenceCount, sizeof(FlatExcitationSignature), bytes) &&
               addArrayPayload(shape.sourceCount, sizeof(FlatSourceSignature), bytes) &&
               addArrayPayload(shape.observationCount, sizeof(ObservationDefinition), bytes) &&
               addArrayPayload(shape.dependencyCount, sizeof(CompiledDependency), bytes);
    }

    bool checkedBoundPayload(const MeasurementShape &shape, std::size_t &bytes) noexcept
    {
        bytes = 0;
        return addArrayPayload(shape.excitationCount, sizeof(PreparedExcitationMapping), bytes) &&
               addArrayPayload(shape.pointIndexCount, sizeof(std::uint32_t), bytes) &&
               addArrayPayload(shape.dependencyCount, sizeof(BoundDependency), bytes) &&
               addArrayPayload(shape.observationCount, sizeof(BoundObservation), bytes);
    }

    bool checkedBuildPayload(const MeasurementShape &shape, std::size_t &bytes) noexcept
    {
        bytes = 0;
        return addArrayPayload(shape.observationSampleCount, sizeof(std::complex<double>), bytes) &&
               addArrayPayload(shape.observationSampleCount, sizeof(ObservationSampleStatus), bytes) &&
               addArrayPayload(shape.dependencyCount, sizeof(RawWaveSeriesView), bytes);
    }

}  // namespace

bool checkedMeasurementAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max() - left)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedMeasurementMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if ((left != 0) && (right > (std::numeric_limits<std::size_t>::max() / left))) {
        return false;
    }
    result = left * right;
    return true;
}

bool checkedMeasurementAccumulate(std::size_t value, std::size_t &total) noexcept
{
    std::size_t result = 0;
    if (!checkedMeasurementAdd(total, value, result)) {
        return false;
    }
    total = result;
    return true;
}

VnaError measurementError(VnaErrorCode code, std::string description)
{
    VnaError error;
    error.code = code;
    error.description = std::move(description);
    return error;
}

Result<MeasurementPayload> inspectMeasurementPayload(const MeasurementShape &shape, MeasurementLimits limits)
{
    if ((limits.maximumPreparedPayloadBytes == 0) || (limits.maximumBuildPayloadBytes == 0)) {
        return Result<MeasurementPayload>::failure(
            measurementError(VnaErrorCode::InvalidPlan, "measurement payload limits must be nonzero"));
    }
    if (!payloadContainersCanHold(shape)) {
        return Result<MeasurementPayload>::failure(
            measurementError(VnaErrorCode::UnsupportedPlan, "measurement element count exceeds max_size"));
    }
    std::size_t compiledBytes = 0;
    std::size_t boundBytes = 0;
    MeasurementPayload payload;
    if (!checkedCompiledPayload(shape, compiledBytes) || !checkedBoundPayload(shape, boundBytes) ||
        !checkedMeasurementAdd(compiledBytes, boundBytes, payload.preparedBytes) ||
        !checkedBuildPayload(shape, payload.buildBytes)) {
        return Result<MeasurementPayload>::failure(
            measurementError(VnaErrorCode::UnsupportedPlan, "measurement payload size overflows"));
    }
    if ((payload.preparedBytes > limits.maximumPreparedPayloadBytes) ||
        (payload.buildBytes > limits.maximumBuildPayloadBytes)) {
        return Result<MeasurementPayload>::failure(
            measurementError(VnaErrorCode::UnsupportedPlan, "measurement payload exceeds configured limits"));
    }
    return Result<MeasurementPayload>::success(payload);
}

Result<bool> preflightMeasurementRequestCapacity(const MeasurementRequest &request, MeasurementLimits limits)
{
    MeasurementShape minimumShape;
    minimumShape.pointCount = request.sweepTemplate.points.size();
    minimumShape.observationCount = request.observations.size();
    auto payload = inspectMeasurementPayload(minimumShape, limits);
    if (!payload.hasValue()) {
        return Result<bool>::failure(payload.error());
    }
    for (const auto &point : request.sweepTemplate.points) {
        if (!checkedMeasurementAccumulate(point.excitations.size(), minimumShape.excitationOccurrenceCount)) {
            return Result<bool>::failure(
                measurementError(VnaErrorCode::UnsupportedPlan, "excitation occurrence count overflows"));
        }
    }
    minimumShape.pointIndexCount = minimumShape.excitationOccurrenceCount;
    payload = inspectMeasurementPayload(minimumShape, limits);
    if (!payload.hasValue()) {
        return Result<bool>::failure(payload.error());
    }
    for (const auto &point : request.sweepTemplate.points) {
        for (const auto &excitation : point.excitations) {
            if (!checkedMeasurementAccumulate(excitation.sources.size(), minimumShape.sourceCount)) {
                return Result<bool>::failure(
                    measurementError(VnaErrorCode::UnsupportedPlan, "measurement source count overflows"));
            }
        }
    }
    payload = inspectMeasurementPayload(minimumShape, limits);
    if (!payload.hasValue()) {
        return Result<bool>::failure(payload.error());
    }
    return Result<bool>::success(true);
}

}  // namespace vna::detail
