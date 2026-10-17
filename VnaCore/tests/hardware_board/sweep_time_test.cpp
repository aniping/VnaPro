/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

#include "support/controlled_device.h"
#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {
namespace {

VnaCore::SweepPlan ManualPlan(double seconds)
{
    auto plan = Plan({3U, 1U, 4U, 2U});
    plan.sweepTimeAuto = false;
    plan.sweepTimeSeconds = seconds;
    return plan;
}

void VerifyManualDeadline(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "", 1U, 1U);
    const auto plan = ManualPlan(0.8);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Require(core.ReadView().manualSweepTimeSupported, "VN10 must expose software pacing support");
    Success(core.Configure(plan));
    Success(core.CreateChannel(2U));
    Success(core.Configure(Plan({1U}), 2U));
    const auto other = Success(core.Start(VnaCore::RunMode::MANUAL, 2U));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto began = std::chrono::steady_clock::now();
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    const auto stale = core.Stop(run.runId + 1U);
    const auto wrongChannel = core.Stop(other.runId, 2U);
    environment.AwaitEvent("GET_BEGIN 0 16 1024");
    const auto firstRead = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    environment.ReleaseRead();
    Success(trigger);
    Require(!Await(stale).ok && Await(wrongChannel).ok, "stop identity checks changed");
    Require(firstRead >= 0.09, "stale or wrong-channel Stop bypassed the matching channel's read deadline");
    const auto completed = core.ReadView();
    Require(completed.lastSweepTimeSeconds >= 0.8 && !completed.sweepProgress,
        "manual target was not applied to real readout or a subsecond sweep published progress");
    VerifyFrame(completed.frame, plan, 1U);
    Success(core.Stop(run.runId));
    Success(core.Configure(plan));
    Require(core.ReadView().lastSweepTimeSeconds == 0.0, "configuration change retained an old measured duration");
    Require(core.Close().ok, "manual pacing shutdown failed");
    environment.VerifyThreadAndEnvironment();
}

void VerifyStopDrainsWithoutTargetWait(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace);
    const auto plan = ManualPlan(100000.0);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("START");
    const auto began = std::chrono::steady_clock::now();
    const auto stop = core.Stop(run.runId);
    Success(trigger);
    Success(stop);
    Require(std::chrono::steady_clock::now() - began < std::chrono::seconds(2),
        "matching Stop waited for the 100000-second software target");
    VerifyFrame(core.ReadView().frame, plan, 1U);
    const auto events = environment.Events();
    Require(std::count(events.begin(), events.end(), "GET_END 0 64 4096") == 1 &&
                std::count(events.begin(), events.end(), "GET_END 128 2 128") == 1,
        "cancelled pacing did not resume full 64-point native drain");
    Require(core.ReadView().state == VnaCore::CoreState::READY, "Stop did not preserve the complete-frame barrier");
    Require(core.Close().ok, "Stop left the native scan completion unknown");
    environment.VerifyThreadAndEnvironment();
}

void VerifyCloseDrainsWithoutTargetWait(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace);
    const auto plan = ManualPlan(100000.0);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("START");
    const auto began = std::chrono::steady_clock::now();
    Require(core.Close().ok, "Close failed to drain a paced native sweep");
    Success(trigger);
    Require(std::chrono::steady_clock::now() - began < std::chrono::seconds(2),
        "Close waited for the software target instead of draining native data");
    VerifyFrame(core.ReadView().frame, plan, 1U);
    Require(core.ReadView().state == VnaCore::CoreState::CLOSED, "Close did not publish Closed after the final Get");
    environment.VerifyThreadAndEnvironment();
}

void VerifyChannelDeadlines(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace);
    const auto first = ManualPlan(0.25);
    const auto second = ManualPlan(0.35);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(first));
    Success(core.CreateChannel(2U));
    Success(core.Configure(second, 2U));
    const auto run1 = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto run2 = Success(core.Start(VnaCore::RunMode::MANUAL, 2U));
    const auto trigger1 = core.TriggerOnce(run1.runId, run1.configRevision);
    const auto trigger2 = core.TriggerOnce(run2.runId, run2.configRevision, 2U);
    Success(trigger1);
    Success(trigger2);
    Require(core.ReadView().lastSweepTimeSeconds >= 0.25 && core.ReadView(2U).lastSweepTimeSeconds >= 0.35,
        "channel readout deadlines or measured durations shared the previous channel's start time");
    VerifyFrame(core.ReadView().frame, first, 1U);
    VerifyFrame(core.ReadView(2U).frame, second, 2U);
    Require(core.Close().ok, "channel pacing shutdown failed");
}

void VerifyConfigurationInterruptsOnlyItsReadout(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace);
    const auto paced = ManualPlan(100000.0);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(paced));
    Success(core.CreateChannel(2U));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("START");
    auto invalid = paced;
    invalid.sweepTimeSeconds = 0.0;
    Require(!Await(core.Configure(invalid)).ok, "invalid configuration was enqueued");
    const auto otherConfiguration = core.Configure(Plan({1U}), 2U);
    Require(trigger.wait_for(std::chrono::milliseconds(80)) == std::future_status::timeout,
        "invalid or other-channel configuration bypassed the active readout deadline");
    const auto replacement = Plan({3U, 1U, 4U, 2U});
    const auto configured = core.Configure(replacement);
    Success(trigger);
    Success(otherConfiguration);
    const auto next = Success(configured);
    const auto retained = core.ReadView().frame;
    VerifyFrame(retained, paced, 1U);
    Require(retained->configRevision == run.configRevision && core.ReadView().configRevision == next.configRevision &&
                next.configRevision > run.configRevision && core.ReadView().lastSweepTimeSeconds == 0.0,
        "new configuration was applied before draining the preceding complete frame");
    Success(core.TriggerOnce(run.runId, next.configRevision));
    VerifyFrame(core.ReadView().frame, replacement, 2U);
    VerifyFrame(retained, paced, 1U);
    Require(core.Close().ok, "configuration interrupt left native completion unknown");
    environment.VerifyThreadAndEnvironment();
}

void VerifyAutoDoesNotUseManualTarget(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace);
    auto plan = Plan({3U, 1U, 4U, 2U});
    plan.sweepTimeSeconds = 100000.0;
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("GET_BEGIN 0 64 4096");
    Success(trigger);
    VerifyFrame(core.ReadView().frame, plan, 1U);
    Require(core.ReadView().lastSweepTimeSeconds > 0.0, "Auto did not record actual complete readout duration");
    const auto events = environment.Events();
    Require(std::count(events.begin(), events.end(), "GET_END 128 2 128") == 1,
        "Auto changed the established 64-point native read layout");
}

void VerifyUnsupportedAndInvalidTiming()
{
    auto owned = std::make_unique<VnaCoreTest::ControlledDevice>();
    auto *device = owned.get();
    VnaCore::AcquisitionCore core(std::move(owned));
    Success(core.Initialize());
    const auto plan = Plan({1U, 2U});
    const auto configured = Success(core.Configure(plan));
    Require(!core.ReadView().manualSweepTimeSupported, "default device invented software pacing support");
    Require(!Await(core.Configure(ManualPlan(1.0))).ok && device->applyPlanCalls_ == 1U,
        "unsupported manual time reached the native configuration");
    for (const auto seconds :
        {0.0, -1.0, 100000.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = plan;
        invalid.sweepTimeSeconds = seconds;
        Require(!Await(core.Configure(invalid)).ok, "invalid timing target was accepted");
    }
    Require(core.ReadView().configRevision == configured.configRevision && device->applyPlanCalls_ == 1U,
        "rejected timing changed acquisition identity or called the device");
}

}  // 匿名命名空间
}  // 命名空间 HardwareBoardTest

int main(int argc, char **argv)
{
    try {
        HardwareBoardTest::Require(argc == 3, "usage: sweep time test <independent ABI probe> <trace prefix>");
        const std::string library = argv[1];
        const std::string trace = argv[2];
        HardwareBoardTest::VerifyManualDeadline(library, trace + "-deadline");
        HardwareBoardTest::VerifyStopDrainsWithoutTargetWait(library, trace + "-stop");
        HardwareBoardTest::VerifyCloseDrainsWithoutTargetWait(library, trace + "-close");
        HardwareBoardTest::VerifyChannelDeadlines(library, trace + "-channels");
        HardwareBoardTest::VerifyConfigurationInterruptsOnlyItsReadout(library, trace + "-configure");
        HardwareBoardTest::VerifyAutoDoesNotUseManualTarget(library, trace + "-auto");
        HardwareBoardTest::VerifyUnsupportedAndInvalidTiming();
        std::cout << "software sweep timing passed: real Get deadlines, scoped Stop, Close drain, channel isolation "
                     "and Auto\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
