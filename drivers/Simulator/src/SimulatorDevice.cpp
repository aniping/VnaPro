#include "SimulatorDevice.h"

#include <cmath>
#include <set>
#include <string>

namespace vna::simulator {
namespace {
    // 与真实驱动一样，模拟器也给所有 ABI 数组设置硬上限，便于测试宿主的失败路径。
    constexpr std::size_t maximumPoints = 1000000;
    constexpr std::size_t maximumExcitations = 16;
    constexpr std::size_t maximumSources = 4;
    constexpr std::size_t maximumWaves = 8;
    constexpr std::uint32_t knownPolicyFlags = VNA_POLICY_ALLOW_QUANTIZATION | VNA_POLICY_ALLOW_MULTIPLE_SWEEPS |
                                               VNA_POLICY_REQUIRE_PHASE_COHERENCE |
                                               VNA_POLICY_REQUIRE_ATOMIC_CONFIGURATION;
    void logMessage(const VnaDriverHostApiV1 &hostApi, std::uint32_t level, const char *message)
    {
        hostApi.Log(level, VnaStringViewV1{message, std::char_traits<char>::length(message)});
    }
    bool validFrequency(double value)
    {
        return std::isfinite(value) && (value >= 1000.0) && (value <= 20.0e9);
    }

    bool validIfBandwidth(double value)
    {
        return std::isfinite(value) && (value >= 1.0) && (value <= 10.0e6);
    }

    bool validArray(std::size_t count, std::size_t maximum, const void *items)
    {
        // 先校验计数和空指针，后续复制循环才允许解引用 ABI 数组。
        return (count > 0) && (count <= maximum) && (items != nullptr);
    }

    bool validPointView(const VnaPointPlanViewV1 &source)
    {
        return (source.reserved == 0) && validFrequency(source.frequencyHz) && validIfBandwidth(source.ifBandwidthHz) &&
               (source.dwellNanoseconds >= 0) &&
               validArray(source.excitationCount, maximumExcitations, source.excitations);
    }

    bool validPort(std::uint16_t port)
    {
        return (port > 0) && (port <= 4);
    }

    bool validSource(const VnaSourcePlanV1 &source)
    {
        const bool validPower =
            std::isfinite(source.powerDbm) && (source.powerDbm >= -100.0) && (source.powerDbm <= 20.0);
        return (source.reserved == 0) && validPort(source.port) && validPower && std::isfinite(source.phaseDegrees);
    }

    bool validWave(const VnaWaveKeyV1 &wave)
    {
        const bool validKind = (wave.kind == VNA_WAVE_INCIDENT) || (wave.kind == VNA_WAVE_OUTGOING);
        return (wave.reserved == 0) && validKind && validPort(wave.port);
    }

    bool validPrepareArguments(const VnaSweepPlanViewV1 *plan, const VnaPrepareResultViewV1 *result)
    {
        const bool validPlan = (plan != nullptr) && (plan->structSize >= sizeof(*plan));
        const bool validResult =
            (result != nullptr) && (result->structSize >= sizeof(*result)) && (result->reserved == 0);
        return validPlan && validResult;
    }

    bool validTriggerMode(std::uint32_t triggerMode)
    {
        // 当前模拟器没有外部触发输入，不能只靠能力位拒绝后继续接受该计划。
        return triggerMode == VNA_TRIGGER_INTERNAL;
    }

    bool uniformDwellTime(const VnaSweepPlanViewV1 &plan)
    {
        // 模拟器未声明逐点驻留能力，直接 ABI 调用也必须保持整次扫描的驻留值一致。
        const std::int64_t dwell = plan.points[0].dwellNanoseconds;
        for (std::size_t index = 1; index < plan.pointCount; ++index) {
            if (plan.points[index].dwellNanoseconds != dwell) {
                return false;
            }
        }
        return true;
    }

    bool validPlanHeader(const VnaSweepPlanViewV1 &plan)
    {
        const bool validPoints = validArray(plan.pointCount, maximumPoints, plan.points);
        const bool validPolicies = (plan.policyFlags & ~knownPolicyFlags) == 0;
        return validPoints && (plan.reserved == 0) && validTriggerMode(plan.triggerMode) && validPolicies &&
               uniformDwellTime(plan);
    }

    bool validExcitationView(const VnaExcitationPlanViewV1 &source)
    {
        return (source.reserved == 0) && validArray(source.sourceCount, maximumSources, source.sources) &&
               validArray(source.requiredWaveCount, maximumWaves, source.requiredWaves);
    }

    VnaStatusV1 copySources(const VnaExcitationPlanViewV1 &source, Excitation &destination)
    {
        // 同一激励不能在同一端口配置两个源，否则相位/功率含义不确定。
        std::set<std::uint16_t> sourcePorts;
        for (std::size_t index = 0; index < source.sourceCount; ++index) {
            const auto &item = source.sources[index];
            if (!validSource(item)) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator source");
            }
            if (!sourcePorts.insert(item.port).second) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "duplicate simulator source port");
            }
            destination.sources.push_back(Source{item.port, item.powerDbm, item.phaseDegrees});
        }
        return simulatorStatus(VNA_STATUS_OK, "");
    }

    VnaStatusV1 copyWaves(const VnaExcitationPlanViewV1 &source, Excitation &destination)
    {
        // 波键由“种类 + 端口”唯一标识，重复项会破坏 waveIndex 到数据列的映射。
        std::set<std::pair<std::uint32_t, std::uint16_t>> waves;
        for (std::size_t index = 0; index < source.requiredWaveCount; ++index) {
            const auto &item = source.requiredWaves[index];
            if (!validWave(item)) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator wave");
            }
            if (!waves.insert({item.kind, item.port}).second) {
                return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "duplicate simulator wave");
            }
            destination.waves.push_back(Wave{item.kind, item.port});
        }
        return simulatorStatus(VNA_STATUS_OK, "");
    }

}  // namespace

VnaStatusV1 simulatorStatus(std::uint32_t code, const char *message)
{
    // 当前消息均为静态字面量，返回视图不会引用栈内存。
    VnaStatusV1 result{};
    result.code = code;
    result.message = VnaStringViewV1{message, std::char_traits<char>::length(message)};
    return result;
}

SimulatorDevice::SimulatorDevice(VnaDriverHostApiV1 hostApi) : hostApi_(hostApi)
{
}

VnaStatusV1 SimulatorDevice::connect()
{
    if (connected_) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator is already connected");
    }
    connected_ = true;
    logMessage(hostApi_, VNA_LOG_INFO, "simulator connected");
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::disconnect()
{
    // 不在活动扫描中强行清空计划，避免 execute 线程读取被释放的容器。
    if (activeSweep_.load() != 0) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator sweep is running");
    }
    connected_ = false;
    prepared_ = false;
    points_.clear();
    layoutWaves_.clear();
    layoutExcitations_.clear();
    layoutViews_.clear();
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::capabilities(VnaCapabilitiesViewV1 *view) const
{
    if (!connected_) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "simulator is not connected");
    }
    if ((view == nullptr) || (view->structSize < sizeof(*view)) || (view->reserved != 0) ||
        (view->reservedFlags != 0)) {
        return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator capabilities view");
    }
    // 能力表与下面的 prepare 校验范围保持同源，避免“宣称支持但实际拒绝”。
    view->structSize = static_cast<std::uint32_t>(sizeof(*view));
    view->portCount = 4;
    view->minimumFrequencyHz = 1000.0;
    view->maximumFrequencyHz = 20.0e9;
    view->minimumIfBandwidthHz = 1.0;
    view->maximumIfBandwidthHz = 10.0e6;
    view->minimumPowerDbm = -100.0;
    view->maximumPowerDbm = 20.0;
    view->maximumPoints = maximumPoints;
    view->maximumExcitationsPerPoint = maximumExcitations;
    view->preferredBatchPoints = simulatorBatchPoints;
    view->featureFlags = VNA_CAPABILITY_PER_POINT_IF_BANDWIDTH | VNA_CAPABILITY_PER_POINT_POWER |
                         VNA_CAPABILITY_SIMULTANEOUS_SOURCES | VNA_CAPABILITY_ABORT | VNA_CAPABILITY_INTERNAL_TRIGGER;
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::prepare(const VnaSweepPlanViewV1 *plan, VnaPrepareResultViewV1 *result)
{
    if (!canPrepare()) {
        return simulatorStatus(VNA_STATUS_INVALID_STATE, "prepare requires a connected idle simulator");
    }
    if (!validPrepareArguments(plan, result)) {
        return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid prepare arguments");
    }
    // 事务保存上一份已准备配置，任一步验证失败都会自动恢复，避免半更新状态。
    PrepareTransaction transaction(*this);
    // ABI 传入的所有视图只在本次调用内有效，执行前必须完整复制到驱动自有存储。
    const VnaStatusV1 copyStatus = copyPlan(*plan);
    if (copyStatus.code != VNA_STATUS_OK) {
        return copyStatus;
    }
    const VnaStatusV1 layoutStatus = rebuildLayouts(*result);
    if (layoutStatus.code != VNA_STATUS_OK) {
        return layoutStatus;
    }
    transaction.commit();
    logMessage(hostApi_, VNA_LOG_DEBUG, "simulator plan prepared");
    return simulatorStatus(VNA_STATUS_OK, "");
}

bool SimulatorDevice::canPrepare() const
{
    return connected_ && (activeSweep_.load() == 0);
}

VnaStatusV1 SimulatorDevice::copyPlan(const VnaSweepPlanViewV1 &plan)
{
    if (!validPlanHeader(plan)) {
        return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator plan header");
    }
    // 先构造局部副本，完整成功后一次性交换到成员，保证强异常安全。
    std::vector<Point> copied;
    copied.reserve(plan.pointCount);
    std::set<std::uint32_t> identifiers;
    for (std::size_t index = 0; index < plan.pointCount; ++index) {
        if (!identifiers.insert(plan.points[index].pointId).second) {
            return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "duplicate simulator point");
        }
        Point point;
        const VnaStatusV1 status = copyPoint(plan.points[index], point);
        if (status.code != VNA_STATUS_OK) {
            return status;
        }
        copied.push_back(std::move(point));
    }
    points_ = std::move(copied);
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::copyPoint(const VnaPointPlanViewV1 &source, Point &destination)
{
    if (!validPointView(source)) {
        return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator point");
    }
    destination.identifier = source.pointId;
    destination.frequencyHz = source.frequencyHz;
    destination.ifBandwidthHz = source.ifBandwidthHz;
    destination.dwellNanoseconds = source.dwellNanoseconds;
    destination.excitations.reserve(source.excitationCount);
    // excitationId 会进入布局表，必须在每个频点内唯一。
    std::set<std::uint16_t> identifiers;
    for (std::size_t index = 0; index < source.excitationCount; ++index) {
        if (!identifiers.insert(source.excitations[index].excitationId).second) {
            return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "duplicate excitation identifier");
        }
        Excitation excitation;
        const VnaStatusV1 status = copyExcitation(source.excitations[index], excitation);
        if (status.code != VNA_STATUS_OK) {
            return status;
        }
        destination.excitations.push_back(std::move(excitation));
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 SimulatorDevice::copyExcitation(const VnaExcitationPlanViewV1 &source, Excitation &destination)
{
    if (!validExcitationView(source)) {
        return simulatorStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid simulator excitation");
    }
    destination.identifier = source.excitationId;
    destination.sources.reserve(source.sourceCount);
    const VnaStatusV1 sourceStatus = copySources(source, destination);
    if (sourceStatus.code != VNA_STATUS_OK) {
        return sourceStatus;
    }
    destination.waves.reserve(source.requiredWaveCount);
    const VnaStatusV1 waveStatus = copyWaves(source, destination);
    if (waveStatus.code != VNA_STATUS_OK) {
        return waveStatus;
    }
    return simulatorStatus(VNA_STATUS_OK, "");
}

}  // namespace vna::simulator
