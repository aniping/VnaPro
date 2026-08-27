#include "MeasurementInternal.h"

namespace vna::detail {
namespace {

    bool validRange(std::size_t offset, std::size_t count, std::size_t size) noexcept
    {
        return (offset <= size) && (count <= (size - offset));
    }

    bool sameWave(VnaWaveKey left, VnaWaveKey right) noexcept
    {
        return (left.kind == right.kind) && (left.port == right.port);
    }

    bool compiledSignatureValid(const CompiledMeasurementData &compiled) noexcept
    {
        for (const auto &point : compiled.points) {
            if (!validRange(point.excitationOffset, point.excitationCount, compiled.excitations.size())) {
                return false;
            }
        }
        for (const auto &excitation : compiled.excitations) {
            if ((excitation.dependencyCount == 0) ||
                !validRange(excitation.sourceOffset, excitation.sourceCount, compiled.sources.size()) ||
                !validRange(excitation.dependencyOffset, excitation.dependencyCount, compiled.dependencies.size())) {
                return false;
            }
            for (std::size_t index = 0; index < excitation.dependencyCount; ++index) {
                if (compiled.dependencies[excitation.dependencyOffset + index].excitationId !=
                    excitation.excitationId) {
                    return false;
                }
            }
        }
        return true;
    }

    bool sameExcitation(
        const VnaExcitationPlan &effective, const FlatExcitationSignature &signature,
        const CompiledMeasurementData &compiled) noexcept
    {
        if ((effective.excitationId != signature.excitationId) || (effective.sources.size() != signature.sourceCount) ||
            (effective.requiredWaves.size() != signature.dependencyCount)) {
            return false;
        }
        for (std::size_t index = 0; index < signature.sourceCount; ++index) {
            const auto &left = effective.sources[index];
            const auto &right = compiled.sources[signature.sourceOffset + index];
            if ((left.port != right.port) || (left.powerDbm != right.powerDbm) ||
                (left.phaseDegrees != right.phaseDegrees)) {
                return false;
            }
        }
        for (std::size_t index = 0; index < signature.dependencyCount; ++index) {
            if (!sameWave(
                    effective.requiredWaves[index], compiled.dependencies[signature.dependencyOffset + index].wave)) {
                return false;
            }
        }
        return true;
    }

    bool samePlanHeader(const VnaSweepPlan &effective, const FlatPlanHeader &header) noexcept
    {
        return (effective.trigger.mode == header.triggerMode) &&
               (effective.policy.allowQuantization == header.allowQuantization) &&
               (effective.policy.allowMultiplePhysicalSweeps == header.allowMultiplePhysicalSweeps) &&
               (effective.policy.requirePhaseCoherence == header.requirePhaseCoherence) &&
               (effective.policy.requireAtomicConfiguration == header.requireAtomicConfiguration);
    }

    bool samePoint(
        const VnaPointPlan &point, const FlatPointSignature &signature,
        const CompiledMeasurementData &compiled) noexcept
    {
        if ((point.pointId != signature.pointId) || (point.frequencyHz != signature.frequencyHz) ||
            (point.ifBandwidthHz != signature.ifBandwidthHz) ||
            (point.dwellTime.count() != signature.dwellNanoseconds) ||
            (point.excitations.size() != signature.excitationCount)) {
            return false;
        }
        for (std::size_t index = 0; index < signature.excitationCount; ++index) {
            if (!sameExcitation(
                    point.excitations[index], compiled.excitations[signature.excitationOffset + index], compiled)) {
                return false;
            }
        }
        return true;
    }

    bool effectivePlanMatches(const VnaSweepPlan &effective, const CompiledMeasurementData &compiled) noexcept
    {
        if (!samePlanHeader(effective, compiled.header) || (effective.points.size() != compiled.points.size())) {
            return false;
        }
        for (std::size_t pointIndex = 0; pointIndex < effective.points.size(); ++pointIndex) {
            if (!samePoint(effective.points[pointIndex], compiled.points[pointIndex], compiled)) {
                return false;
            }
        }
        return true;
    }

    bool uniqueLayoutIds(const std::vector<VnaWaveLayout> &layouts) noexcept
    {
        for (std::size_t index = 0; index < layouts.size(); ++index) {
            if (layouts[index].layoutId == 0) {
                return false;
            }
            for (std::size_t prior = 0; prior < index; ++prior) {
                if ((layouts[index].layoutId == layouts[prior].layoutId) ||
                    (layouts[index].excitationId == layouts[prior].excitationId)) {
                    return false;
                }
            }
        }
        return true;
    }

}  // namespace

VnaError validateMeasurementPreparedPlan(const CompiledMeasurementData &compiled, const PreparedSweep &hardwarePrepared)
{
    if (!compiledSignatureValid(compiled)) {
        return measurementError(VnaErrorCode::InternalError, "compiled measurement signature is invalid");
    }
    if (!effectivePlanMatches(hardwarePrepared.effectivePlan, compiled)) {
        return measurementError(
            VnaErrorCode::ConfigurationRejected, "effective plan differs from compiled measurement");
    }
    if (hardwarePrepared.report.quantized || (compiled.header.hasRatio && !hardwarePrepared.report.phaseCoherent)) {
        return measurementError(VnaErrorCode::ProtocolError, "prepared execution report violates measurement policy");
    }
    return {};
}

VnaError validateMeasurementLayouts(
    const CompiledMeasurementData &compiled, const std::vector<VnaWaveLayout> &layouts,
    const std::vector<BindingExcitationDescription> &excitations)
{
    if ((layouts.size() != excitations.size()) || !uniqueLayoutIds(layouts)) {
        return measurementError(VnaErrorCode::ProtocolError, "prepared layouts contain missing or duplicate identity");
    }
    for (const auto &excitation : excitations) {
        const VnaWaveLayout *matched = nullptr;
        for (const auto &layout : layouts) {
            if (layout.excitationId == excitation.excitationId) {
                matched = &layout;
            }
        }
        if ((matched == nullptr) || (matched->waveKeys.size() != excitation.dependencyCount)) {
            return measurementError(VnaErrorCode::ProtocolError, "prepared layout wave count is inconsistent");
        }
        for (std::size_t index = 0; index < excitation.dependencyCount; ++index) {
            const auto &dependency = compiled.dependencies[excitation.compiledDependencyOffset + index];
            if (!sameWave(matched->waveKeys[index], dependency.wave)) {
                return measurementError(VnaErrorCode::ProtocolError, "prepared layout wave sequence is inconsistent");
            }
        }
    }
    return {};
}

}  // namespace vna::detail
