/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <limits>
#include <utility>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {
namespace {

void VerifyPowerPair(
    const std::string &library, const std::string &trace, ExpectedPower pattern, double first, double second)
{
    TestEnvironment environment(trace, pattern);
    auto expected = Plan({2U, 4U});
    for (std::size_t point = 0U; point < expected.powerDbm.size(); ++point) {
        expected.powerDbm[point] = point % 2U == 0U ? first : second;
    }
    auto callerPlan = expected;
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    const auto configured = Success(core.Configure(callerPlan));
    callerPlan.powerDbm.assign(TEST_POINT_COUNT, 0.0);
    const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(started.runId, configured.configRevision));
    const auto retained = core.ReadView().frame;
    VerifyFrame(retained, expected, 1U);
    Success(core.Stop(started.runId));
    const auto events = environment.Events();
    for (const double invalid : {-120.01, 100.01, std::numeric_limits<double>::quiet_NaN(),
             std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        auto candidate = expected;
        candidate.powerDbm.back() = invalid;
        const auto rejected = Await(core.Configure(candidate));
        const auto view = core.ReadView();
        Require(!rejected.ok && view.state == VnaCore::CoreState::READY &&
                    view.configRevision == configured.configRevision && view.frame == retained &&
                    environment.Events() == events,
            "非法 dBm 不得触及驱动、使核心故障或替换已提交计划与完整帧");
    }
    const auto restarted = Success(core.Start(VnaCore::RunMode::MANUAL));
    Success(core.TriggerOnce(restarted.runId, configured.configRevision));
    VerifyFrame(core.ReadView().frame, expected, 2U);
    VerifyFrame(retained, expected, 1U);
    Require(core.Close().ok, "dBm 合同验收关闭失败");
    environment.VerifyThreadAndEnvironment();
}

}  // 匿名命名空间

void VerifyProfile(const std::string &library, const std::string &trace, std::vector<std::uint32_t> sources)
{
    TestEnvironment environment(trace, sources.size() == 2U ? ExpectedPower::ZERO : ExpectedPower::RAMP);
    auto expected = Plan(std::move(sources));
    if (expected.sourcePorts.size() == 2U) {
        expected.powerDbm.assign(TEST_POINT_COUNT, 0.0);
    }
    auto callerPlan = expected;
    VnaCore::FramePtr retained;
    {
        VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
        Success(core.Initialize());
        const auto configured = Success(core.Configure(callerPlan));
        // 触发前改写调用方副本，探针也在 START 时检查 SET 传入数组仍然有效。
        callerPlan.frequencyRaw.assign(TEST_POINT_COUNT, 999U);
        callerPlan.powerDbm.assign(TEST_POINT_COUNT, -88.5);
        const auto started = Success(core.Start(VnaCore::RunMode::MANUAL));
        Success(core.TriggerOnce(started.runId, configured.configRevision));
        retained = core.ReadView().frame;
        VerifyFrame(retained, expected, 1U);
        Success(core.TriggerOnce(started.runId, configured.configRevision));
        VerifyFrame(core.ReadView().frame, expected, 2U);
        Require(retained != core.ReadView().frame && retained->frameSeq == 1U,
            "new hardware sweep modified retained immutable frame");
        Require(core.Close().ok, "hardware shutdown failed");
    }
    VerifyFrame(retained, expected, 1U);
    const auto events = environment.Events();
    const std::vector<std::string> prefix = {"INIT", "RF_OFF", "FREQ 130", "POWER 130", "IFBW 130",
        "PORTS " + std::to_string(expected.sourcePorts.size()), "DOWNLOAD", "RF_ON", "START"};
    Require(
        events.size() == prefix.size() + 6U + 1U + 6U + 1U && std::equal(prefix.begin(), prefix.end(), events.begin()),
        "hardware initialization/configuration order differs");
    const auto capacity = expected.sourcePorts.size() * 16U;
    const std::vector<std::string> segment = {"GET_BEGIN 0 64 " + std::to_string(capacity * 64U),
        "GET_END 0 64 " + std::to_string(capacity * 64U), "GET_BEGIN 64 64 " + std::to_string(capacity * 64U),
        "GET_END 64 64 " + std::to_string(capacity * 64U), "GET_BEGIN 128 2 " + std::to_string(capacity * 2U),
        "GET_END 128 2 " + std::to_string(capacity * 2U)};
    Require(std::equal(segment.begin(), segment.end(), events.begin() + prefix.size()) &&
                events[prefix.size() + segment.size()] == "START" &&
                std::equal(segment.begin(), segment.end(), events.begin() + prefix.size() + segment.size() + 1U) &&
                events.back() == "RF_OFF",
        "hardware segmentation, repeat-start or shutdown order differs");
    Require(std::find(events.begin(), events.end(), "UNLOAD") == events.end(),
        "hardware DLL was unloaded without a driver resource-release API");
    environment.VerifyThreadAndEnvironment();
}

void VerifyPowerDbmContract(const std::string &library, const std::string &trace)
{
    VerifyPowerPair(library, trace + "-fractional", ExpectedPower::FRACTIONAL, -17.5, 0.25);
    VerifyPowerPair(library, trace + "-boundaries", ExpectedPower::BOUNDARIES, -120.0, 100.0);
}

}  // 命名空间 HardwareBoardTest
