#include <gtest/gtest.h>

#include <chrono>
#include <utility>

#include "DriverResultValidation.h"
#include "VnaHardware.h"
#include "VnaValidation.h"

namespace {

vna::VnaCapabilities capabilities()
{
    // 构造允许四端口和多源的基准能力，单个用例只修改与目标规则相关的字段。
    vna::VnaCapabilities result;
    result.portCount = 4;
    result.frequencyHz = {1000.0, 20.0e9};
    result.ifBandwidthHz = {1.0, 10.0e6};
    result.powerDbm = {-100.0, 20.0};
    result.maximumPoints = 100;
    result.maximumExcitationsPerPoint = 4;
    result.simultaneousSources = true;
    result.supportsInternalTrigger = true;
    return result;
}

vna::VnaSweepPlan validPlan()
{
    // 最小合法计划作为差分基线，避免每个拒绝用例同时触发多个无关错误。
    vna::VnaExcitationPlan excitation;
    excitation.excitationId = 1;
    excitation.sources.push_back(vna::VnaSourcePlan{1, 0.0, 0.0});
    excitation.requiredWaves.push_back(vna::VnaWaveKey{vna::WaveKind::Incident, 1});
    vna::VnaPointPlan point;
    point.pointId = 1;
    point.frequencyHz = 1.0e9;
    point.ifBandwidthHz = 1000.0;
    point.excitations.push_back(std::move(excitation));
    vna::VnaSweepPlan plan;
    plan.points.push_back(std::move(point));
    return plan;
}

vna::VnaSweepPlan twoPointPlan()
{
    auto plan = validPlan();
    plan.points.push_back(plan.points.front());
    plan.points.back().pointId = 2;
    return plan;
}

VnaCapabilitiesViewV1 validDriverCapabilities()
{
    // 驱动侧基线视图包含所有必填维度，便于精确测试跨 ABI 结果校验。
    VnaCapabilitiesViewV1 result{};
    result.structSize = sizeof(result);
    result.portCount = 4;
    result.minimumFrequencyHz = 1000.0;
    result.maximumFrequencyHz = 20.0e9;
    result.minimumIfBandwidthHz = 1.0;
    result.maximumIfBandwidthHz = 10.0e6;
    result.minimumPowerDbm = -100.0;
    result.maximumPowerDbm = 20.0;
    result.maximumPoints = 100;
    result.maximumExcitationsPerPoint = 4;
    result.preferredBatchPoints = 2;
    return result;
}

TEST(VnaValidationTest, RejectsPortsOutsideCapabilities)
{
    // 端口号从 1 开始，四端口设备必须拒绝端口 5。
    auto plan = validPlan();
    plan.points.front().excitations.front().sources.front().port = 5;
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsUnknownPublicEnumValues)
{
    // C++ 强转可构造越界枚举，宿主不得将它们默认映射成合法 ABI 值。
    auto plan = validPlan();
    plan.trigger.mode = static_cast<vna::TriggerMode>(UINT8_C(0xff));
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));

    plan = validPlan();
    plan.points.front().excitations.front().requiredWaves.front().kind = static_cast<vna::WaveKind>(UINT8_C(0xff));
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsExternalTriggerWhenCapabilityIsAbsent)
{
    auto plan = validPlan();
    plan.trigger.mode = vna::TriggerMode::External;
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsLayoutChangesForOneExcitation)
{
    // 同一 excitationId 跨频点改变 requiredWaves 会破坏稳定的 layoutId 语义。
    auto plan = validPlan();
    plan.points.push_back(plan.points.front());
    plan.points.back().pointId = 2;
    plan.points.back().excitations.front().requiredWaves.front().port = 2;
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsUnsupportedPerPointIfBandwidth)
{
    auto plan = twoPointPlan();
    plan.points.back().ifBandwidthHz = 2000.0;
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsUnsupportedPerPointDwell)
{
    auto plan = twoPointPlan();
    plan.points.back().dwellTime = std::chrono::microseconds(1);
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, RejectsUnsupportedPerPointPower)
{
    auto plan = twoPointPlan();
    plan.points.back().excitations.front().sources.front().powerDbm = -3.0;
    EXPECT_FALSE(vna::isValid(vna::detail::validatePlan(plan, capabilities())));
}

TEST(VnaValidationTest, AllowsVariationsExplicitlySupportedByDevice)
{
    auto plan = twoPointPlan();
    plan.points.back().ifBandwidthHz = 2000.0;
    plan.points.back().dwellTime = std::chrono::microseconds(1);
    plan.points.back().excitations.front().sources.front().powerDbm = -3.0;
    auto deviceCapabilities = capabilities();
    deviceCapabilities.perPointIfBandwidth = true;
    deviceCapabilities.perPointDwell = true;
    deviceCapabilities.perPointPower = true;
    EXPECT_TRUE(vna::isValid(vna::detail::validatePlan(plan, deviceCapabilities)));
}

TEST(DriverResultValidationTest, RequiresAbortCallbackWhenCapabilityPromisesAbort)
{
    // 能力位和函数表必须一致，避免运行扫描后才发现无法执行中止。
    auto driverCapabilities = validDriverCapabilities();
    driverCapabilities.featureFlags = VNA_CAPABILITY_ABORT;
    EXPECT_FALSE(vna::detail::driverCapabilitiesAreValid(driverCapabilities, false));
    EXPECT_TRUE(vna::detail::driverCapabilitiesAreValid(driverCapabilities, true));
}

TEST(DriverResultValidationTest, RejectsReservedAndUnknownCapabilityBits)
{
    auto driverCapabilities = validDriverCapabilities();
    driverCapabilities.reserved = 1;
    EXPECT_FALSE(vna::detail::driverCapabilitiesAreValid(driverCapabilities, true));
    driverCapabilities.reserved = 0;
    driverCapabilities.reservedFlags = 1;
    EXPECT_FALSE(vna::detail::driverCapabilitiesAreValid(driverCapabilities, true));
    driverCapabilities.reservedFlags = 0;
    driverCapabilities.featureFlags = UINT32_C(0x80000000);
    EXPECT_FALSE(vna::detail::driverCapabilitiesAreValid(driverCapabilities, true));
}

TEST(DriverResultValidationTest, RejectsQuantizationUntilActualPlanCanBeReturned)
{
    // V1 无法回传实际量化计划，即使策略允许量化也必须失败关闭。
    VnaPrepareResultViewV1 result{};
    result.structSize = sizeof(result);
    result.physicalSweepCount = 1;
    result.preferredBatchPoints = 1;
    result.executionFlags = VNA_EXECUTION_QUANTIZED | VNA_EXECUTION_ATOMIC | VNA_EXECUTION_PHASE_COHERENT;
    vna::SweepPolicy policy;
    policy.allowQuantization = true;
    EXPECT_EQ(vna::detail::validateDriverExecutionReport(result, policy).code, vna::VnaErrorCode::ProtocolError);
}

TEST(DriverResultValidationTest, RejectsReservedPrepareAndLayoutFields)
{
    VnaPrepareResultViewV1 result{};
    result.structSize = sizeof(result);
    result.physicalSweepCount = 1;
    result.preferredBatchPoints = 1;
    result.executionFlags = VNA_EXECUTION_ATOMIC | VNA_EXECUTION_PHASE_COHERENT;
    result.reserved = 1;
    EXPECT_EQ(
        vna::detail::validateDriverExecutionReport(result, vna::SweepPolicy{}).code, vna::VnaErrorCode::ProtocolError);

    const VnaWaveKeyV1 wave{VNA_WAVE_INCIDENT, 1, 0};
    const VnaWaveLayoutViewV1 layout{1, 1, 1, 1, &wave};
    result.reserved = 0;
    result.layoutCount = 1;
    result.layouts = &layout;
    EXPECT_FALSE(vna::detail::copyDriverLayouts(result).hasValue());
}

}  // namespace
