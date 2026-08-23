#include "VnaPreparedCapacity.h"

#include <cstddef>
#include <string>
#include <utility>

#include "VnaCheckedSize.h"
#include "VnaDriverLimits.h"

namespace vna::detail {
namespace {

    class PayloadAccumulator final {
    public:
        explicit PayloadAccumulator(std::size_t maximum) : maximum_(maximum)
        {
        }

        bool addElements(std::size_t count, std::size_t elementBytes) noexcept
        {
            std::size_t bytes = 0;
            return checkedSizeMultiply(count, elementBytes, bytes) && addBytes(bytes);
        }

        bool addBytes(std::size_t bytes) noexcept
        {
            std::size_t next = 0;
            if (!checkedSizeAdd(total_, bytes, next)) {
                return false;
            }
            if (next > maximum_) {
                limitExceeded_ = true;
                return false;
            }
            total_ = next;
            return true;
        }

        std::size_t total() const noexcept
        {
            return total_;
        }

        bool limitExceeded() const noexcept
        {
            return limitExceeded_;
        }

    private:
        const std::size_t maximum_{0};
        std::size_t total_{0};
        bool limitExceeded_{false};
    };

    VnaError capacityError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    bool addPlanPayload(const VnaSweepPlan &plan, PayloadAccumulator &payload)
    {
        // 参数按值进入异步所有权域，预留但未使用的 capacity 同样是宿主常驻内存。
        if (!payload.addElements(plan.points.capacity(), sizeof(VnaPointPlan))) {
            return false;
        }
        for (const auto &point : plan.points) {
            if (!payload.addElements(point.excitations.capacity(), sizeof(VnaExcitationPlan))) {
                return false;
            }
            for (const auto &excitation : point.excitations) {
                if (!payload.addElements(excitation.sources.capacity(), sizeof(VnaSourcePlan)) ||
                    !payload.addElements(excitation.requiredWaves.capacity(), sizeof(VnaWaveKey))) {
                    return false;
                }
            }
        }
        return true;
    }

    bool validDriverLayoutHeader(const VnaPrepareResultViewV1 &view)
    {
        return (view.layoutCount > 0) && (view.layoutCount <= maximumDriverLayouts) && (view.layouts != nullptr);
    }

    bool addDriverLayouts(const VnaPrepareResultViewV1 &view, PayloadAccumulator &payload)
    {
        if (!validDriverLayoutHeader(view) || !payload.addElements(view.layoutCount, sizeof(VnaWaveLayout))) {
            return false;
        }
        for (std::size_t index = 0; index < view.layoutCount; ++index) {
            const auto &layout = view.layouts[index];
            if ((layout.waveCount == 0) || (layout.waveCount > maximumDriverWavesPerLayout) ||
                (layout.waveKeys == nullptr) || !payload.addElements(layout.waveCount, sizeof(VnaWaveKey))) {
                return false;
            }
        }
        return true;
    }

    bool addPreparedLayouts(const PreparedSweep &prepared, PayloadAccumulator &payload)
    {
        if (!payload.addElements(prepared.layouts.capacity(), sizeof(VnaWaveLayout)) ||
            !payload.addElements(prepared.report.warnings.capacity(), sizeof(ValidationIssue))) {
            return false;
        }
        for (const auto &layout : prepared.layouts) {
            if (!payload.addElements(layout.waveKeys.capacity(), sizeof(VnaWaveKey))) {
                return false;
            }
        }
        for (const auto &warning : prepared.report.warnings) {
            if (!payload.addBytes(warning.path.capacity()) || !payload.addBytes(warning.description.capacity())) {
                return false;
            }
        }
        return true;
    }

}  // namespace

Result<std::size_t> inspectRequestedPlanPayload(const VnaSweepPlan &plan, std::size_t maximumBytes)
{
    PayloadAccumulator payload(maximumBytes);
    if (!addPlanPayload(plan, payload)) {
        const char *description = payload.limitExceeded() ? "requested plan payload exceeds the deployment limit"
                                                          : "requested plan payload is not representable";
        return Result<std::size_t>::failure(capacityError(VnaErrorCode::UnsupportedPlan, description));
    }
    return Result<std::size_t>::success(payload.total());
}

Result<std::size_t> inspectDriverPreparedPayload(
    const VnaSweepPlan &plan, const VnaPrepareResultViewV1 &view, std::size_t maximumBytes)
{
    PayloadAccumulator payload(maximumBytes);
    if (!addPlanPayload(plan, payload)) {
        const char *description = payload.limitExceeded() ? "prepared sweep payload exceeds the deployment limit"
                                                          : "effective plan payload is not representable";
        return Result<std::size_t>::failure(capacityError(VnaErrorCode::ConfigurationRejected, description));
    }
    if (!addDriverLayouts(view, payload)) {
        if (payload.limitExceeded()) {
            return Result<std::size_t>::failure(capacityError(
                VnaErrorCode::ConfigurationRejected, "prepared sweep payload exceeds the deployment limit"));
        }
        return Result<std::size_t>::failure(
            capacityError(VnaErrorCode::ProtocolError, "driver returned invalid or unrepresentable wave layouts"));
    }
    return Result<std::size_t>::success(payload.total());
}

Result<std::size_t> inspectPreparedSweepPayload(const PreparedSweep &prepared, std::size_t maximumBytes)
{
    PayloadAccumulator payload(maximumBytes);
    if (!addPlanPayload(prepared.effectivePlan, payload) || !addPreparedLayouts(prepared, payload)) {
        const char *description = payload.limitExceeded() ? "prepared sweep payload exceeds the deployment limit"
                                                          : "prepared sweep payload is not representable";
        return Result<std::size_t>::failure(capacityError(VnaErrorCode::ConfigurationRejected, description));
    }
    return Result<std::size_t>::success(payload.total());
}

}  // namespace vna::detail
