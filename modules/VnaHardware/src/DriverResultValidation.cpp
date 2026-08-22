#include "DriverResultValidation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace vna::detail {
namespace {

    // 这些上限是主程序的防御边界：插件数据不可信，不能让畸形计数触发巨量分配。
    constexpr std::size_t maximumLayouts = 1024;
    constexpr std::size_t maximumWavesPerLayout = 256;
    constexpr std::size_t maximumBatchPoints = 1048576;

    VnaError localError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    bool validWaveArray(const VnaWaveLayoutViewV1 &source)
    {
        return (source.waveCount > 0) && (source.waveCount <= maximumWavesPerLayout) && (source.waveKeys != nullptr);
    }

    bool knownWaveKind(std::uint32_t kind)
    {
        return (kind == VNA_WAVE_INCIDENT) || (kind == VNA_WAVE_OUTGOING);
    }

    Result<std::vector<VnaWaveKey>> copyWaveKeys(const VnaWaveLayoutViewV1 &source)
    {
        // ABI 视图中的指针由驱动借出，必须先验证计数和地址，再逐项深拷贝。
        if (!validWaveArray(source)) {
            return Result<std::vector<VnaWaveKey>>::failure(
                localError(VnaErrorCode::ProtocolError, "driver returned an invalid wave layout"));
        }
        std::vector<VnaWaveKey> waves;
        waves.reserve(source.waveCount);
        for (std::size_t index = 0; index < source.waveCount; ++index) {
            const auto &key = source.waveKeys[index];
            if (!knownWaveKind(key.kind)) {
                return Result<std::vector<VnaWaveKey>>::failure(
                    localError(VnaErrorCode::ProtocolError, "driver returned an unknown wave kind"));
            }
            const WaveKind kind = key.kind == VNA_WAVE_INCIDENT ? WaveKind::Incident : WaveKind::Outgoing;
            waves.push_back(VnaWaveKey{kind, key.port});
        }
        return Result<std::vector<VnaWaveKey>>::success(std::move(waves));
    }

    bool sameWave(const VnaWaveKey &left, const VnaWaveKey &right)
    {
        return (left.kind == right.kind) && (left.port == right.port);
    }

    VnaError registerLayoutIdentifiers(
        const VnaWaveLayout &layout, std::set<LayoutId> &layoutIdentifiers,
        std::set<ExcitationId> &excitationIdentifiers)
    {
        // layoutId 用于数据块寻址，excitationId 用于回填计划；两者都必须一一对应。
        if ((layout.layoutId == 0) || !layoutIdentifiers.insert(layout.layoutId).second) {
            return localError(VnaErrorCode::ProtocolError, "driver returned a duplicate layout identifier");
        }
        if (!excitationIdentifiers.insert(layout.excitationId).second) {
            return localError(VnaErrorCode::ProtocolError, "driver returned a duplicate excitation layout");
        }
        return {};
    }

    VnaError compareWaveSequence(const std::vector<VnaWaveKey> &expected, const std::vector<VnaWaveKey> &actual)
    {
        if (expected.size() != actual.size()) {
            return localError(VnaErrorCode::ProtocolError, "driver layout does not match the plan");
        }
        // 波次序决定二维缓冲区的 waveIndex，集合相同但顺序不同也会造成数据错位。
        for (std::size_t index = 0; index < actual.size(); ++index) {
            if (!sameWave(expected[index], actual[index])) {
                return localError(VnaErrorCode::ProtocolError, "driver changed the requested wave layout");
            }
        }
        return {};
    }

    VnaError validateLayoutShape(
        const VnaWaveLayout &layout, const std::map<ExcitationId, std::vector<VnaWaveKey>> &expected,
        std::set<LayoutId> &layoutIdentifiers, std::set<ExcitationId> &excitationIdentifiers)
    {
        const VnaError identifierError = registerLayoutIdentifiers(layout, layoutIdentifiers, excitationIdentifiers);
        if (identifierError.code != VnaErrorCode::None) {
            return identifierError;
        }
        const auto match = expected.find(layout.excitationId);
        if (match == expected.end()) {
            return localError(VnaErrorCode::ProtocolError, "driver layout does not match the plan");
        }
        return compareWaveSequence(match->second, layout.waveKeys);
    }

    bool executionDimensionsAreValid(const VnaPrepareResultViewV1 &view)
    {
        // 批大小同时参与缓冲区乘法，除非落在宿主上限内，否则拒绝执行报告。
        const bool validBatch = (view.preferredBatchPoints > 0) && (view.preferredBatchPoints <= maximumBatchPoints);
        return (view.structSize >= sizeof(view)) && (view.physicalSweepCount > 0) && validBatch;
    }

    bool reportsQuantization(const VnaPrepareResultViewV1 &view)
    {
        return (view.executionFlags & VNA_EXECUTION_QUANTIZED) != 0;
    }

    bool sweepCountViolated(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy)
    {
        return (view.physicalSweepCount > 1) && !policy.allowMultiplePhysicalSweeps;
    }

    bool phaseCoherenceViolated(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy)
    {
        return policy.requirePhaseCoherence && ((view.executionFlags & VNA_EXECUTION_PHASE_COHERENT) == 0);
    }

    bool atomicityViolated(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy)
    {
        return policy.requireAtomicConfiguration && ((view.executionFlags & VNA_EXECUTION_ATOMIC) == 0);
    }

    bool capabilityRangesAreFinite(const VnaCapabilitiesViewV1 &view)
    {
        // NaN 会绕过普通大小比较，正负无穷也不能作为可执行的硬件边界。
        const std::array<double, 6> values{view.minimumFrequencyHz,   view.maximumFrequencyHz,
                                           view.minimumIfBandwidthHz, view.maximumIfBandwidthHz,
                                           view.minimumPowerDbm,      view.maximumPowerDbm};
        return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
    }

    bool capabilityRangesAreOrdered(const VnaCapabilitiesViewV1 &view)
    {
        const std::array<std::pair<double, double>, 3> ranges{
            std::make_pair(view.minimumFrequencyHz, view.maximumFrequencyHz),
            std::make_pair(view.minimumIfBandwidthHz, view.maximumIfBandwidthHz),
            std::make_pair(view.minimumPowerDbm, view.maximumPowerDbm)};
        return std::all_of(ranges.begin(), ranges.end(), [](const auto &range) { return range.first <= range.second; });
    }

    bool capabilityDimensionsAreValid(const VnaCapabilitiesViewV1 &view)
    {
        const bool validBatch = (view.preferredBatchPoints > 0) && (view.preferredBatchPoints <= maximumBatchPoints);
        const std::array<bool, 5> dimensions{
            view.structSize >= sizeof(view), view.portCount > 0, view.maximumPoints > 0,
            view.maximumExcitationsPerPoint > 0, validBatch};
        return std::all_of(dimensions.begin(), dimensions.end(), [](bool valid) { return valid; });
    }

    bool validLayoutArray(const VnaPrepareResultViewV1 &view)
    {
        return (view.layoutCount > 0) && (view.layoutCount <= maximumLayouts) && (view.layouts != nullptr);
    }

    std::map<ExcitationId, std::vector<VnaWaveKey>> expectedLayouts(const VnaSweepPlan &plan)
    {
        // 同一激励在各频点的波布局已由计划校验保证一致，这里只保留首份作为契约模板。
        std::map<ExcitationId, std::vector<VnaWaveKey>> expected;
        for (const auto &point : plan.points) {
            for (const auto &excitation : point.excitations) {
                expected.emplace(excitation.excitationId, excitation.requiredWaves);
            }
        }
        return expected;
    }

    VnaError validateExecutionPolicy(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy)
    {
        if (sweepCountViolated(view, policy)) {
            return localError(
                VnaErrorCode::ProtocolError, "driver split a plan into multiple sweeps without permission");
        }
        if (phaseCoherenceViolated(view, policy)) {
            return localError(VnaErrorCode::ProtocolError, "driver lost required phase coherence");
        }
        if (atomicityViolated(view, policy)) {
            return localError(VnaErrorCode::ProtocolError, "driver lost required configuration atomicity");
        }
        return {};
    }

}  // namespace

VnaCapabilities copyDriverCapabilities(const VnaCapabilitiesViewV1 &view)
{
    // 调用者已完成 ABI 视图校验；此处将短生命周期视图转换为宿主自有值对象。
    VnaCapabilities result;
    result.portCount = view.portCount;
    result.frequencyHz = {view.minimumFrequencyHz, view.maximumFrequencyHz};
    result.ifBandwidthHz = {view.minimumIfBandwidthHz, view.maximumIfBandwidthHz};
    result.powerDbm = {view.minimumPowerDbm, view.maximumPowerDbm};
    result.maximumPoints = view.maximumPoints;
    result.maximumExcitationsPerPoint = view.maximumExcitationsPerPoint;
    result.preferredBatchPoints = view.preferredBatchPoints;
    result.perPointIfBandwidth = (view.featureFlags & VNA_CAPABILITY_PER_POINT_IF_BANDWIDTH) != 0;
    result.perPointPower = (view.featureFlags & VNA_CAPABILITY_PER_POINT_POWER) != 0;
    result.perPointDwell = (view.featureFlags & VNA_CAPABILITY_PER_POINT_DWELL) != 0;
    result.simultaneousSources = (view.featureFlags & VNA_CAPABILITY_SIMULTANEOUS_SOURCES) != 0;
    result.supportsAbort = (view.featureFlags & VNA_CAPABILITY_ABORT) != 0;
    result.supportsInternalTrigger = (view.featureFlags & VNA_CAPABILITY_INTERNAL_TRIGGER) != 0;
    result.supportsExternalTrigger = (view.featureFlags & VNA_CAPABILITY_EXTERNAL_TRIGGER) != 0;
    return result;
}

bool driverCapabilitiesAreValid(const VnaCapabilitiesViewV1 &view, bool abortCallbackAvailable)
{
    // 驱动只有在同时提供 abort 回调时才能声明中止能力，避免运行期出现空函数指针。
    const bool abortContractValid = ((view.featureFlags & VNA_CAPABILITY_ABORT) == 0) || abortCallbackAvailable;
    return capabilityRangesAreFinite(view) && capabilityRangesAreOrdered(view) && capabilityDimensionsAreValid(view) &&
           abortContractValid;
}

Result<std::vector<VnaWaveLayout>> copyDriverLayouts(const VnaPrepareResultViewV1 &view)
{
    if (!validLayoutArray(view)) {
        return Result<std::vector<VnaWaveLayout>>::failure(
            localError(VnaErrorCode::ProtocolError, "driver returned invalid wave layouts"));
    }
    // prepare 返回后驱动可立即复用其数组，所以宿主在离开调用栈前完成两层深拷贝。
    std::vector<VnaWaveLayout> layouts;
    layouts.reserve(view.layoutCount);
    for (std::size_t index = 0; index < view.layoutCount; ++index) {
        auto waves = copyWaveKeys(view.layouts[index]);
        if (!waves.hasValue()) {
            return Result<std::vector<VnaWaveLayout>>::failure(waves.error());
        }
        layouts.push_back(
            VnaWaveLayout{view.layouts[index].layoutId, view.layouts[index].excitationId, waves.takeValue()});
    }
    return Result<std::vector<VnaWaveLayout>>::success(std::move(layouts));
}

VnaError validateDriverLayouts(const std::vector<VnaWaveLayout> &layouts, const VnaSweepPlan &plan)
{
    const auto expected = expectedLayouts(plan);
    std::set<LayoutId> layoutIdentifiers;
    std::set<ExcitationId> excitationIdentifiers;
    for (const auto &layout : layouts) {
        const VnaError error = validateLayoutShape(layout, expected, layoutIdentifiers, excitationIdentifiers);
        if (error.code != VnaErrorCode::None) {
            return error;
        }
    }
    // 逐项匹配只能发现多余或错误布局，最终计数检查负责发现驱动遗漏的激励。
    if (excitationIdentifiers.size() != expected.size()) {
        return localError(VnaErrorCode::ProtocolError, "driver omitted a requested wave layout");
    }
    return {};
}

VnaError validateDriverExecutionReport(const VnaPrepareResultViewV1 &view, const SweepPolicy &policy)
{
    if (!executionDimensionsAreValid(view)) {
        return localError(VnaErrorCode::ProtocolError, "driver returned an invalid execution report");
    }
    // 量化结果必须携带实际采用的计划；V1 ABI 尚无该返回通道，因此只能失败关闭。
    if (reportsQuantization(view)) {
        return localError(VnaErrorCode::ProtocolError, "driver cannot return a quantized plan without actual values");
    }
    return validateExecutionPolicy(view, policy);
}

}  // namespace vna::detail
