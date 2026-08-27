#include "MeasurementInternal.h"

#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

namespace vna {
namespace {

    using BindResult = Result<std::shared_ptr<const PreparedMeasurement>>;

    bool sameWave(VnaWaveKey left, VnaWaveKey right) noexcept
    {
        return (left.kind == right.kind) && (left.port == right.port);
    }

    template <typename Excitation>
    std::size_t findExcitation(const std::vector<Excitation> &excitations, ExcitationId excitationId) noexcept
    {
        for (std::size_t index = 0; index < excitations.size(); ++index) {
            if (excitations[index].excitationId == excitationId) {
                return index;
            }
        }
        return excitations.size();
    }

    bool collectExcitations(
        const detail::CompiledMeasurementData &compiled, std::vector<detail::BindingExcitationDescription> &excitations)
    {
        for (const auto &signature : compiled.excitations) {
            const std::size_t found = findExcitation(excitations, signature.excitationId);
            if (found == excitations.size()) {
                excitations.push_back(
                    detail::BindingExcitationDescription{
                        signature.excitationId, signature.dependencyOffset, signature.dependencyCount, 1});
            } else if (
                (excitations[found].compiledDependencyOffset != signature.dependencyOffset) ||
                (excitations[found].dependencyCount != signature.dependencyCount) ||
                !detail::checkedMeasurementAccumulate(1, excitations[found].pointCount)) {
                return false;
            }
        }
        std::size_t dependencyCount = 0;
        for (const auto &excitation : excitations) {
            if (!detail::checkedMeasurementAccumulate(excitation.dependencyCount, dependencyCount)) {
                return false;
            }
        }
        return dependencyCount == compiled.dependencies.size();
    }

    bool describeShape(
        const detail::CompiledMeasurementData &compiled,
        const std::vector<detail::BindingExcitationDescription> &excitations, detail::MeasurementShape &shape) noexcept
    {
        if (compiled.points.size() > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        for (const auto &observation : compiled.observations) {
            const std::size_t found = findExcitation(excitations, observation.excitationId);
            if ((found == excitations.size()) ||
                !detail::checkedMeasurementAccumulate(excitations[found].pointCount, shape.observationSampleCount)) {
                return false;
            }
        }
        shape = detail::MeasurementShape{compiled.points.size(),       compiled.excitations.size(),
                                         compiled.sources.size(),      excitations.size(),
                                         compiled.dependencies.size(), compiled.observations.size(),
                                         compiled.excitations.size(),  shape.observationSampleCount};
        return true;
    }

    bool fillMappings(
        const detail::CompiledMeasurementData &compiled,
        const std::vector<detail::BindingExcitationDescription> &excitations, detail::PreparedMeasurementData &bound)
    {
        std::size_t pointOffset = 0;
        for (std::size_t mappingIndex = 0; mappingIndex < excitations.size(); ++mappingIndex) {
            const auto &source = excitations[mappingIndex];
            bound.excitations.push_back(
                detail::PreparedExcitationMapping{
                    source.excitationId, pointOffset, source.pointCount, bound.dependencies.size(),
                    source.dependencyCount});
            if (!detail::checkedMeasurementAccumulate(source.pointCount, pointOffset)) {
                return false;
            }
            for (std::size_t index = 0; index < source.dependencyCount; ++index) {
                const auto &dependency = compiled.dependencies[source.compiledDependencyOffset + index];
                bound.dependencies.push_back(
                    detail::BoundDependency{dependency.excitationId, dependency.wave, mappingIndex});
            }
        }
        bound.pointIndices.resize(pointOffset);
        return true;
    }

    bool fillPointIndices(const VnaSweepPlan &effective, detail::PreparedMeasurementData &bound)
    {
        std::vector<std::size_t> filled(bound.excitations.size(), 0);
        for (std::size_t pointIndex = 0; pointIndex < effective.points.size(); ++pointIndex) {
            for (const auto &excitation : effective.points[pointIndex].excitations) {
                const std::size_t mappingIndex = findExcitation(bound.excitations, excitation.excitationId);
                if (mappingIndex == bound.excitations.size()) {
                    return false;
                }
                const auto &mapping = bound.excitations[mappingIndex];
                if (filled[mappingIndex] >= mapping.pointCount) {
                    return false;
                }
                const std::size_t target = mapping.pointIndexOffset + filled[mappingIndex];
                bound.pointIndices[target] = static_cast<std::uint32_t>(pointIndex);
                ++filled[mappingIndex];
            }
        }
        for (std::size_t index = 0; index < filled.size(); ++index) {
            if (filled[index] != bound.excitations[index].pointCount) {
                return false;
            }
        }
        return true;
    }

    std::size_t findDependency(
        const detail::PreparedMeasurementData &bound, ExcitationId excitationId, VnaWaveKey wave) noexcept
    {
        for (std::size_t index = 0; index < bound.dependencies.size(); ++index) {
            const auto &candidate = bound.dependencies[index];
            if ((candidate.excitationId == excitationId) && sameWave(candidate.wave, wave)) {
                return index;
            }
        }
        return bound.dependencies.size();
    }

    bool bindObservations(
        const detail::CompiledMeasurementData &compiled, detail::PreparedMeasurementData &bound) noexcept
    {
        std::size_t outputOffset = 0;
        for (const auto &observation : compiled.observations) {
            const std::size_t mappingIndex = findExcitation(bound.excitations, observation.excitationId);
            if (mappingIndex == bound.excitations.size()) {
                return false;
            }
            detail::BoundObservation result;
            result.id = observation.id;
            result.excitationId = observation.excitationId;
            result.excitationMappingIndex = mappingIndex;
            result.outputOffset = outputOffset;
            result.sampleCount = bound.excitations[mappingIndex].pointCount;
            if (const auto *direct = std::get_if<DirectWaveExpression>(&observation.expression)) {
                result.numeratorDependencyOrdinal = findDependency(bound, observation.excitationId, direct->wave);
            } else if (const auto *ratio = std::get_if<WaveRatioExpression>(&observation.expression)) {
                result.kind = detail::BoundExpressionKind::Ratio;
                result.numeratorDependencyOrdinal = findDependency(bound, observation.excitationId, ratio->numerator);
                result.denominatorDependencyOrdinal =
                    findDependency(bound, observation.excitationId, ratio->denominator);
            } else {
                return false;
            }
            if ((result.numeratorDependencyOrdinal == bound.dependencies.size()) ||
                ((result.kind == detail::BoundExpressionKind::Ratio) &&
                 (result.denominatorDependencyOrdinal == bound.dependencies.size())) ||
                !detail::checkedMeasurementAccumulate(result.sampleCount, outputOffset)) {
                return false;
            }
            bound.observations.push_back(result);
        }
        bound.observationSampleCount = outputOffset;
        return true;
    }

    std::unique_ptr<detail::PreparedMeasurementData> buildPreparedData(
        const detail::CompiledMeasurementData &compiled, const VnaSweepPlan &effective,
        const std::vector<detail::BindingExcitationDescription> &excitations)
    {
        auto bound = std::make_unique<detail::PreparedMeasurementData>();
        bound->excitations.reserve(excitations.size());
        bound->dependencies.reserve(compiled.dependencies.size());
        bound->observations.reserve(compiled.observations.size());
        if (!fillMappings(compiled, excitations, *bound) || !fillPointIndices(effective, *bound) ||
            !bindObservations(compiled, *bound)) {
            return nullptr;
        }
        return bound;
    }

    BindResult bindFailure(VnaErrorCode code, const char *description)
    {
        return BindResult::failure(detail::measurementError(code, description));
    }

    BindResult bareInternalFailure() noexcept
    {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        return BindResult::failure(std::move(error));
    }

    VnaError validateBindingInputs(
        const detail::CompiledMeasurementData &compiled, const PreparedSweep &hardwarePrepared,
        std::vector<detail::BindingExcitationDescription> &excitations, detail::MeasurementShape &shape)
    {
        VnaError validation = detail::validateMeasurementPreparedPlan(compiled, hardwarePrepared);
        if (validation.code != VnaErrorCode::None) {
            return validation;
        }
        if (!collectExcitations(compiled, excitations)) {
            return detail::measurementError(
                VnaErrorCode::InternalError, "compiled measurement dependencies are inconsistent");
        }
        validation = detail::validateMeasurementLayouts(compiled, hardwarePrepared.layouts, excitations);
        if (validation.code != VnaErrorCode::None) {
            return validation;
        }
        if (!describeShape(compiled, excitations, shape)) {
            return detail::measurementError(
                VnaErrorCode::UnsupportedPlan, "measurement binding shape exceeds supported limits");
        }
        auto payload = detail::inspectMeasurementPayload(shape, compiled.header.limits);
        return payload.hasValue() ? VnaError{} : payload.error();
    }

}  // namespace

Result<std::shared_ptr<const PreparedMeasurement>> bindMeasurement(
    std::shared_ptr<const CompiledMeasurement> compiled, std::shared_ptr<const PreparedAcquisition> acquisition)
{
    try {
        if (!compiled || !acquisition) {
            return bindFailure(VnaErrorCode::InvalidPlan, "measurement binding inputs must be non-null");
        }
        if (!compiled->data_) {
            return bindFailure(VnaErrorCode::InternalError, "compiled measurement data is empty");
        }
        const auto &compiledData = *compiled->data_;
        const auto &hardwarePrepared = acquisition->hardwarePrepared();
        std::vector<detail::BindingExcitationDescription> excitations;
        detail::MeasurementShape shape;
        VnaError validation = validateBindingInputs(compiledData, hardwarePrepared, excitations, shape);
        if (validation.code != VnaErrorCode::None) {
            return BindResult::failure(std::move(validation));
        }
        auto bound = buildPreparedData(compiledData, hardwarePrepared.effectivePlan, excitations);
        if (!bound || (bound->observationSampleCount != shape.observationSampleCount)) {
            return bindFailure(VnaErrorCode::InternalError, "measurement binding construction failed");
        }
        auto owned = std::unique_ptr<PreparedMeasurement>(
            new PreparedMeasurement(std::move(compiled), std::move(acquisition), std::move(bound)));
        return BindResult::success(std::shared_ptr<const PreparedMeasurement>(std::move(owned)));
    } catch (const std::bad_alloc &) {
        return bareInternalFailure();
    } catch (const std::length_error &) {
        return bareInternalFailure();
    } catch (...) {
        return bareInternalFailure();
    }
}

}  // namespace vna
