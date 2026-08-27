#include "MeasurementInternal.h"
#include "VnaWaveData.h"

#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace vna {
namespace {
    using FrameResult = Result<std::shared_ptr<const ObservationFrame>>;
    struct CalculatedSample {
        std::complex<double> value{};
        ObservationSampleStatus status{};
    };
    std::uint32_t qualityBit(CalculationQuality quality) noexcept
    {
        return static_cast<std::uint32_t>(quality);
    }
    bool hasMissing(std::uint32_t quality) noexcept
    {
        return (quality & static_cast<std::uint32_t>(WaveQuality::Missing)) != 0;
    }
    bool isFinite(const std::complex<double> &value) noexcept
    {
        return std::isfinite(value.real()) && std::isfinite(value.imag());
    }
    std::complex<double> unavailableValue() noexcept
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }
    bool validRange(std::size_t offset, std::size_t count, std::size_t size) noexcept
    {
        return (offset <= size) && (count <= (size - offset));
    }
    bool mappingHeaderIsValid(
        const detail::PreparedExcitationMapping &mapping, const detail::PreparedMeasurementData &data,
        std::size_t nextPoint, std::size_t nextDependency) noexcept
    {
        return (mapping.pointCount != 0) && (mapping.dependencyCount != 0) && (mapping.pointIndexOffset == nextPoint) &&
               (mapping.dependencyOffset == nextDependency) &&
               validRange(mapping.pointIndexOffset, mapping.pointCount, data.pointIndices.size()) &&
               validRange(mapping.dependencyOffset, mapping.dependencyCount, data.dependencies.size());
    }
    bool mappingDependenciesAreValid(
        const detail::PreparedExcitationMapping &mapping, const detail::PreparedMeasurementData &data,
        std::size_t mappingIndex) noexcept
    {
        for (std::size_t ordinal = mapping.dependencyOffset;
             ordinal < mapping.dependencyOffset + mapping.dependencyCount; ++ordinal) {
            const auto &dependency = data.dependencies[ordinal];
            if ((dependency.excitationId != mapping.excitationId) ||
                (dependency.excitationMappingIndex != mappingIndex)) {
                return false;
            }
        }
        return true;
    }
    bool mappingsAreValid(const detail::PreparedMeasurementData &data) noexcept
    {
        std::size_t nextPoint = 0;
        std::size_t nextDependency = 0;
        for (std::size_t index = 0; index < data.excitations.size(); ++index) {
            const auto &mapping = data.excitations[index];
            if (!mappingHeaderIsValid(mapping, data, nextPoint, nextDependency) ||
                !mappingDependenciesAreValid(mapping, data, index)) {
                return false;
            }
            nextPoint += mapping.pointCount;
            nextDependency += mapping.dependencyCount;
        }
        return (nextPoint == data.pointIndices.size()) && (nextDependency == data.dependencies.size());
    }
    bool dependencyMatches(
        const detail::PreparedMeasurementData &data, std::size_t ordinal, std::size_t mappingIndex) noexcept
    {
        if ((ordinal >= data.dependencies.size()) || (mappingIndex >= data.excitations.size())) {
            return false;
        }
        const auto &dependency = data.dependencies[ordinal];
        const auto &mapping = data.excitations[mappingIndex];
        return (dependency.excitationMappingIndex == mappingIndex) && (dependency.excitationId == mapping.excitationId);
    }
    bool observationIsValid(
        const detail::PreparedMeasurementData &data, const detail::BoundObservation &observation,
        std::size_t expectedOffset) noexcept
    {
        if ((observation.id == 0) || (observation.outputOffset != expectedOffset) ||
            !validRange(observation.outputOffset, observation.sampleCount, data.observationSampleCount) ||
            (observation.excitationMappingIndex >= data.excitations.size())) {
            return false;
        }
        const auto &mapping = data.excitations[observation.excitationMappingIndex];
        if ((observation.excitationId != mapping.excitationId) || (observation.sampleCount != mapping.pointCount) ||
            !dependencyMatches(data, observation.numeratorDependencyOrdinal, observation.excitationMappingIndex)) {
            return false;
        }
        if (observation.kind == detail::BoundExpressionKind::Direct) {
            return true;
        }
        return (observation.kind == detail::BoundExpressionKind::Ratio) &&
               dependencyMatches(data, observation.denominatorDependencyOrdinal, observation.excitationMappingIndex);
    }
    bool observationsAreValid(const detail::PreparedMeasurementData &data) noexcept
    {
        std::size_t nextOffset = 0;
        ObservationId previousId = 0;
        for (const auto &observation : data.observations) {
            if ((observation.id <= previousId) || !observationIsValid(data, observation, nextOffset)) {
                return false;
            }
            previousId = observation.id;
            nextOffset += observation.sampleCount;
        }
        return !data.observations.empty() && (nextOffset == data.observationSampleCount);
    }
    bool preparedDataIsValid(const detail::PreparedMeasurementData &data) noexcept
    {
        return (data.observationSampleCount != 0) && !data.excitations.empty() && !data.dependencies.empty() &&
               mappingsAreValid(data) && observationsAreValid(data);
    }
    bool viewMatches(
        const RawWaveSeriesView &view, const detail::PreparedMeasurementData &data,
        const detail::PreparedExcitationMapping &mapping) noexcept
    {
        if ((view.size != mapping.pointCount) || (view.size == 0) || (view.pointIndices == nullptr) ||
            (view.values == nullptr) || (view.quality == nullptr)) {
            return false;
        }
        for (std::size_t index = 0; index < view.size; ++index) {
            if (view.pointIndices[index] != data.pointIndices[mapping.pointIndexOffset + index]) {
                return false;
            }
        }
        return true;
    }
    bool resolveDependencies(
        const detail::PreparedMeasurementData &data, const RawSweepFrame &rawFrame,
        std::vector<RawWaveSeriesView> &scratch) noexcept
    {
        bool valid = true;
        for (std::size_t ordinal = 0; ordinal < data.dependencies.size(); ++ordinal) {
            const auto &dependency = data.dependencies[ordinal];
            const auto resolved = rawFrame.findWave(dependency.excitationId, dependency.wave);
            if (!resolved) {
                valid = false;
                continue;
            }
            scratch[ordinal] = *resolved;
            const auto &mapping = data.excitations[dependency.excitationMappingIndex];
            valid = viewMatches(*resolved, data, mapping) && valid;
        }
        return valid;
    }
    CalculatedSample calculateDirect(std::complex<double> value, std::uint32_t quality) noexcept
    {
        CalculatedSample result;
        result.status.numeratorWaveQuality = quality;
        if (hasMissing(quality)) {
            result.value = unavailableValue();
            result.status.calculationQuality = qualityBit(CalculationQuality::InputUnavailable);
        } else if (!isFinite(value)) {
            result.value = unavailableValue();
            result.status.calculationQuality = qualityBit(CalculationQuality::ValueNonFinite);
        } else {
            result.value = value;
        }
        return result;
    }
    bool isExactlyZero(const std::complex<double> &value) noexcept
    {
        return (value.real() == 0.0) && (value.imag() == 0.0);
    }
    std::uint32_t ratioInputQuality(
        const std::complex<double> &numerator, std::uint32_t numeratorQuality, const std::complex<double> &denominator,
        std::uint32_t denominatorQuality) noexcept
    {
        std::uint32_t result = qualityBit(CalculationQuality::Valid);
        const bool numeratorAvailable = !hasMissing(numeratorQuality);
        const bool denominatorAvailable = !hasMissing(denominatorQuality);
        if (!numeratorAvailable || !denominatorAvailable) {
            result |= qualityBit(CalculationQuality::InputUnavailable);
        }
        if (numeratorAvailable && !isFinite(numerator)) {
            result |= qualityBit(CalculationQuality::ValueNonFinite);
        }
        if (denominatorAvailable && !isFinite(denominator)) {
            result |= qualityBit(CalculationQuality::DenominatorNonFinite);
        } else if (denominatorAvailable && isExactlyZero(denominator)) {
            result |= qualityBit(CalculationQuality::DenominatorZero);
        }
        return result;
    }
    CalculatedSample calculateRatio(
        std::complex<double> numerator, std::uint32_t numeratorQuality, std::complex<double> denominator,
        std::uint32_t denominatorQuality) noexcept
    {
        CalculatedSample result;
        result.status.numeratorWaveQuality = numeratorQuality;
        result.status.denominatorWaveQuality = denominatorQuality;
        result.status.calculationQuality =
            ratioInputQuality(numerator, numeratorQuality, denominator, denominatorQuality);
        if (result.status.calculationQuality != qualityBit(CalculationQuality::Valid)) {
            result.value = unavailableValue();
            return result;
        }
        result.value = numerator / denominator;
        if (!isFinite(result.value)) {
            result.value = unavailableValue();
            result.status.calculationQuality = qualityBit(CalculationQuality::ResultNonFinite);
        }
        return result;
    }
    void buildDirect(
        const detail::BoundObservation &observation, const RawWaveSeriesView &input,
        detail::ObservationFrameData &frame) noexcept
    {
        for (std::size_t index = 0; index < observation.sampleCount; ++index) {
            const CalculatedSample sample = calculateDirect(input.values[index], input.quality[index]);
            frame.values[observation.outputOffset + index] = sample.value;
            frame.status[observation.outputOffset + index] = sample.status;
        }
    }
    void buildRatio(
        const detail::BoundObservation &observation, const RawWaveSeriesView &numerator,
        const RawWaveSeriesView &denominator, detail::ObservationFrameData &frame) noexcept
    {
        for (std::size_t index = 0; index < observation.sampleCount; ++index) {
            const CalculatedSample sample = calculateRatio(
                numerator.values[index], numerator.quality[index], denominator.values[index],
                denominator.quality[index]);
            frame.values[observation.outputOffset + index] = sample.value;
            frame.status[observation.outputOffset + index] = sample.status;
        }
    }
    void populateFrame(
        const detail::PreparedMeasurementData &prepared, const std::vector<RawWaveSeriesView> &scratch,
        detail::ObservationFrameData &frame) noexcept
    {
        for (const auto &observation : prepared.observations) {
            const auto &numerator = scratch[observation.numeratorDependencyOrdinal];
            if (observation.kind == detail::BoundExpressionKind::Direct) {
                buildDirect(observation, numerator, frame);
            } else {
                const auto &denominator = scratch[observation.denominatorDependencyOrdinal];
                buildRatio(observation, numerator, denominator, frame);
            }
        }
    }
    FrameResult failure(VnaErrorCode code, const char *description)
    {
        return FrameResult::failure(detail::measurementError(code, description));
    }
    FrameResult internalFailure()
    {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        return FrameResult::failure(std::move(error));
    }
}  // namespace

Result<std::shared_ptr<const ObservationFrame>> buildObservationFrame(
    std::shared_ptr<const PreparedMeasurement> prepared, const RawSweepFrame &rawFrame)
{
    try {
        if (!prepared) {
            return failure(VnaErrorCode::InvalidPlan, "prepared measurement is empty");
        }
        if (!prepared->compiled_ || !prepared->acquisition_ || !prepared->data_) {
            return failure(VnaErrorCode::InternalError, "prepared measurement is incomplete");
        }
        const auto &rawPrepared = rawFrame.prepared();
        const auto &boundPrepared = *prepared->acquisition_;
        if ((&rawPrepared != &boundPrepared) ||
            (rawPrepared.hardwarePrepared().configurationId != boundPrepared.hardwarePrepared().configurationId)) {
            return failure(VnaErrorCode::ConfigurationExpired, "raw frame belongs to another configuration");
        }
        if (!preparedDataIsValid(*prepared->data_)) {
            return failure(VnaErrorCode::InternalError, "prepared measurement mapping is invalid");
        }
        auto data = std::make_unique<detail::ObservationFrameData>();
        data->runId = rawFrame.runId();
        data->sweepIndex = rawFrame.sweepIndex();
        data->hardwareSweepId = rawFrame.hardwareSweepId();
        data->values.resize(prepared->data_->observationSampleCount);
        data->status.resize(prepared->data_->observationSampleCount);
        std::vector<RawWaveSeriesView> scratch(prepared->data_->dependencies.size());
        if (!resolveDependencies(*prepared->data_, rawFrame, scratch)) {
            return failure(VnaErrorCode::ProtocolError, "raw frame wave schema does not match the binding");
        }
        populateFrame(*prepared->data_, scratch, *data);
        auto *raw = new ObservationFrame(std::move(prepared), std::move(data));
        return FrameResult::success(std::shared_ptr<const ObservationFrame>(raw));
    } catch (...) {
        return internalFailure();
    }
}
}  // namespace vna
