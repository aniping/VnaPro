#include "MeasurementInternal.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace vna {
namespace {

    struct RequirementLocation {
        std::size_t requirementIndex{0};
        std::size_t dependencyOffset{0};
    };

    using RequirementLookup = std::unordered_map<ExcitationId, RequirementLocation>;

    void reserveCompiledStorage(const detail::MeasurementShape &shape, detail::CompiledMeasurementData &data)
    {
        data.points.reserve(shape.pointCount);
        data.excitations.reserve(shape.excitationOccurrenceCount);
        data.sources.reserve(shape.sourceCount);
        data.observations.reserve(shape.observationCount);
        data.dependencies.reserve(shape.dependencyCount);
    }

    detail::FlatPlanHeader makePlanHeader(const VnaSweepPlan &plan, MeasurementLimits limits, bool hasRatio)
    {
        detail::FlatPlanHeader header;
        header.limits = limits;
        header.triggerMode = plan.trigger.mode;
        header.allowQuantization = plan.policy.allowQuantization;
        header.allowMultiplePhysicalSweeps = plan.policy.allowMultiplePhysicalSweeps;
        header.requirePhaseCoherence = plan.policy.requirePhaseCoherence;
        header.requireAtomicConfiguration = plan.policy.requireAtomicConfiguration;
        header.hasRatio = hasRatio;
        return header;
    }

    RequirementLookup appendDependencies(const detail::CompilationModel &model, detail::CompiledMeasurementData &data)
    {
        RequirementLookup lookup;
        lookup.reserve(model.excitations.size());
        for (std::size_t index = 0; index < model.excitations.size(); ++index) {
            const auto &requirement = model.excitations[index];
            const RequirementLocation location{index, data.dependencies.size()};
            if (!lookup.emplace(requirement.excitationId, location).second) {
                throw std::logic_error("compiled excitation requirements are duplicated");
            }
            for (const auto wave : requirement.waves) {
                data.dependencies.push_back(detail::CompiledDependency{requirement.excitationId, wave});
            }
        }
        return lookup;
    }

    const detail::ExcitationRequirements &requirementFor(
        ExcitationId excitationId, const RequirementLookup &lookup, const detail::CompilationModel &model)
    {
        const auto location = lookup.find(excitationId);
        if ((location == lookup.end()) || (location->second.requirementIndex >= model.excitations.size())) {
            throw std::logic_error("compiled excitation requirement is missing");
        }
        return model.excitations[location->second.requirementIndex];
    }

    void appendSources(const VnaExcitationPlan &excitation, detail::CompiledMeasurementData &data)
    {
        for (const auto &source : excitation.sources) {
            data.sources.push_back(detail::FlatSourceSignature{source.port, source.powerDbm, source.phaseDegrees});
        }
    }

    void appendExcitation(
        VnaExcitationPlan &excitation, const RequirementLookup &lookup, const detail::CompilationModel &model,
        detail::CompiledMeasurementData &data)
    {
        const auto location = lookup.find(excitation.excitationId);
        if (location == lookup.end()) {
            throw std::logic_error("sweep excitation requirement is missing");
        }
        const auto &requirement = requirementFor(excitation.excitationId, lookup, model);
        detail::FlatExcitationSignature signature;
        signature.excitationId = excitation.excitationId;
        signature.sourceOffset = data.sources.size();
        signature.sourceCount = excitation.sources.size();
        signature.dependencyOffset = location->second.dependencyOffset;
        signature.dependencyCount = requirement.waves.size();
        appendSources(excitation, data);
        excitation.requiredWaves = requirement.waves;
        data.excitations.push_back(signature);
    }

    void appendPoint(
        VnaPointPlan &point, const RequirementLookup &lookup, const detail::CompilationModel &model,
        detail::CompiledMeasurementData &data)
    {
        detail::FlatPointSignature signature;
        signature.pointId = point.pointId;
        signature.frequencyHz = point.frequencyHz;
        signature.ifBandwidthHz = point.ifBandwidthHz;
        signature.dwellNanoseconds = static_cast<std::int64_t>(point.dwellTime.count());
        signature.excitationOffset = data.excitations.size();
        signature.excitationCount = point.excitations.size();
        for (auto &excitation : point.excitations) {
            appendExcitation(excitation, lookup, model, data);
        }
        data.points.push_back(signature);
    }

    bool compiledShapeMatches(
        const detail::CompiledMeasurementData &data, const detail::MeasurementShape &shape) noexcept
    {
        return (data.points.size() == shape.pointCount) &&
               (data.excitations.size() == shape.excitationOccurrenceCount) &&
               (data.sources.size() == shape.sourceCount) && (data.observations.size() == shape.observationCount) &&
               (data.dependencies.size() == shape.dependencyCount);
    }

    std::unique_ptr<detail::CompiledMeasurementData> buildCompiledData(
        MeasurementRequest &request, const detail::CompilationModel &model, MeasurementLimits limits)
    {
        if (model.hasRatio) {
            request.sweepTemplate.policy.requirePhaseCoherence = true;
        }
        auto data = std::make_unique<detail::CompiledMeasurementData>();
        reserveCompiledStorage(model.shape, *data);
        const RequirementLookup lookup = appendDependencies(model, *data);
        for (auto &point : request.sweepTemplate.points) {
            appendPoint(point, lookup, model, *data);
        }
        data->observations = request.observations;
        std::sort(data->observations.begin(), data->observations.end(), [](const auto &left, const auto &right) {
            return left.id < right.id;
        });
        data->header = makePlanHeader(request.sweepTemplate, limits, model.hasRatio);
        if (!compiledShapeMatches(*data, model.shape)) {
            throw std::logic_error("compiled measurement shape changed during construction");
        }
        return data;
    }

    Result<MeasurementCompilation> compilationInternalFailure() noexcept
    {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        return Result<MeasurementCompilation>::failure(std::move(error));
    }

}  // namespace

Result<MeasurementCompilation> compileMeasurement(MeasurementRequest &&request, MeasurementLimits limits)
{
    try {
        auto inspected = detail::inspectMeasurementRequest(request, limits);
        if (!inspected.hasValue()) {
            return Result<MeasurementCompilation>::failure(inspected.error());
        }
        auto data = buildCompiledData(request, inspected.value(), limits);
        std::unique_ptr<const detail::CompiledMeasurementData> immutableData(std::move(data));
        auto *raw = new CompiledMeasurement(std::move(immutableData));
        std::shared_ptr<const CompiledMeasurement> compiled(raw);
        MeasurementCompilation compilation;
        compilation.sweepPlan = std::move(request.sweepTemplate);
        compilation.compiled = std::move(compiled);
        return Result<MeasurementCompilation>::success(std::move(compilation));
    } catch (const std::bad_alloc &) {
        return compilationInternalFailure();
    } catch (const std::length_error &) {
        return compilationInternalFailure();
    } catch (...) {
        return compilationInternalFailure();
    }
}

}  // namespace vna
