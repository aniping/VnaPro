#include "AcquisitionCapacity.h"

#include <complex>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>

namespace vna::detail {
namespace {

    using LayoutWaveCounts = std::unordered_map<ExcitationId, std::size_t>;

    VnaError sizeError(const char *description)
    {
        return acquisitionError(VnaErrorCode::UnsupportedPlan, description);
    }

    Result<bool> recordExcitation(
        const VnaExcitationPlan &excitation, LayoutWaveCounts &waveCounts, AcquisitionShape &shape,
        const AcquisitionLimits &limits)
    {
        AcquisitionShape candidate = shape;
        if (!checkedAccumulate(excitation.requiredWaves.size(), candidate.waveSampleCount)) {
            return Result<bool>::failure(sizeError("wave sample count overflows"));
        }
        const auto existing = waveCounts.find(excitation.excitationId);
        if (existing == waveCounts.end()) {
            if (!checkedAccumulate(1, candidate.layoutCount) ||
                !checkedAccumulate(excitation.requiredWaves.size(), candidate.waveMappingCount)) {
                return Result<bool>::failure(sizeError("prepared mapping count overflows"));
            }
        } else if (existing->second < excitation.requiredWaves.size()) {
            const std::size_t additionalWaves = excitation.requiredWaves.size() - existing->second;
            if (!checkedAccumulate(additionalWaves, candidate.waveMappingCount)) {
                return Result<bool>::failure(sizeError("wave mapping count overflows"));
            }
        }
        auto bounded = inspectAcquisitionShape(candidate, limits);
        if (!bounded.hasValue()) {
            return Result<bool>::failure(bounded.error());
        }
        if (existing == waveCounts.end()) {
            waveCounts.emplace(excitation.excitationId, excitation.requiredWaves.size());
        } else if (existing->second < excitation.requiredWaves.size()) {
            existing->second = excitation.requiredWaves.size();
        }
        shape = candidate;
        return Result<bool>::success(true);
    }

    Result<AcquisitionShape> describeRequestedPlan(const VnaSweepPlan &plan, const AcquisitionLimits &limits)
    {
        if (plan.points.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<AcquisitionShape>::failure(sizeError("plan point count exceeds uint32 point indices"));
        }
        constexpr std::size_t minimumWaveSampleBytes = sizeof(std::complex<double>) + sizeof(std::uint32_t);
        if (plan.points.size() > (limits.maximumRawFramePayloadBytes / minimumWaveSampleBytes)) {
            return Result<AcquisitionShape>::failure(sizeError("plan point count exceeds acquisition payload limit"));
        }
        AcquisitionShape shape;
        LayoutWaveCounts waveCounts;
        for (const auto &point : plan.points) {
            AcquisitionShape candidate = shape;
            if (!checkedAccumulate(point.excitations.size(), candidate.pointExcitationRecordCount)) {
                return Result<AcquisitionShape>::failure(sizeError("point-excitation record count overflows"));
            }
            auto bounded = inspectAcquisitionShape(candidate, limits);
            if (!bounded.hasValue()) {
                return Result<AcquisitionShape>::failure(bounded.error());
            }
            shape = candidate;
            for (const auto &excitation : point.excitations) {
                auto recorded = recordExcitation(excitation, waveCounts, shape, limits);
                if (!recorded.hasValue()) {
                    return Result<AcquisitionShape>::failure(recorded.error());
                }
            }
        }
        return Result<AcquisitionShape>::success(shape);
    }

}  // namespace

Result<AcquisitionPlanInspection> inspectRequestedPlan(const VnaSweepPlan &plan, const AcquisitionLimits &limits)
{
    try {
        auto shape = describeRequestedPlan(plan, limits);
        if (!shape.hasValue()) {
            return Result<AcquisitionPlanInspection>::failure(shape.error());
        }
        return inspectAcquisitionShape(shape.value(), limits);
    } catch (const std::bad_alloc &) {
        return Result<AcquisitionPlanInspection>::failure(sizeError("requested plan inspection allocation failed"));
    } catch (const std::length_error &) {
        return Result<AcquisitionPlanInspection>::failure(
            sizeError("requested plan inspection exceeds container max_size"));
    } catch (...) {
        return Result<AcquisitionPlanInspection>::failure(
            acquisitionError(VnaErrorCode::InternalError, "requested plan inspection failed"));
    }
}

}  // namespace vna::detail
