/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <utility>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {

void VerifyProfile(const std::string &library, const std::string &trace, std::vector<std::uint32_t> sources)
{
    TestEnvironment environment(trace);
    auto expected = Plan(std::move(sources));
    if (expected.sourcePorts.size() == 2U) {
        expected.powerRaw.assign(TEST_POINT_COUNT, 0U);
    }
    auto callerPlan = expected;
    VnaCore::FramePtr retained;
    {
        VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
        Success(core.Initialize());
        const auto configured = Success(core.Configure(callerPlan));
        // 触发前改写调用方副本，探针也在 START 时检查 SET 传入数组仍然有效。
        callerPlan.frequencyRaw.assign(TEST_POINT_COUNT, 999U);
        callerPlan.powerRaw.assign(TEST_POINT_COUNT, 888U);
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

}  // 命名空间 HardwareBoardTest
