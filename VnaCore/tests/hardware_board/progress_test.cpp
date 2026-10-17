/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include "test_environment.h"
#include "test_support.h"

namespace HardwareBoardTest {
namespace {

void VerifyProgressPlan(const VnaCore::SweepPlan &actual, const VnaCore::SweepPlan &expected)
{
    Require(actual.frequencyRaw == expected.frequencyRaw && actual.frequencyHz == expected.frequencyHz &&
                actual.powerDbm == expected.powerDbm && actual.sourcePorts == expected.sourcePorts,
        "progress plan does not retain the full acquisition axis");
}

void VerifyProgressPort(const VnaCore::PortFrame &port, std::size_t completed, std::uint32_t sweep)
{
    for (std::size_t point = 0U; point < port.iq.size() / 16U; ++point) {
        for (std::size_t component = 0U; component < 16U; ++component) {
            const auto value = port.iq[point * 16U + component];
            const double expected = sweep * 1000000.0 + port.sourcePort * 100000.0 + point * 100.0 + component;
            Require(point < completed ? value == expected : std::isnan(value),
                "progress contains incorrect IQ or fabricated unmeasured suffix values");
        }
    }
}

void VerifyProgress(const VnaCore::CoreView &view, const VnaCore::SweepPlan &plan, std::uint32_t sweep)
{
    const auto progress = view.sweepProgress;
    Require(progress && progress->frame && progress->completedPoints == 128U,
        "slow sweep lost the real completed prefix while its next native segment is blocked");
    const auto &partial = *progress->frame;
    VerifyProgressPlan(partial.plan, plan);
    Require(partial.frameSeq == sweep && partial.runId == view.runId && partial.configRevision == view.configRevision &&
                partial.channelId == view.channelId,
        "progress identity differs from its running channel");
    Require(partial.ports.size() == plan.sourcePorts.size(), "progress source count differs from its plan");
    for (std::size_t source = 0U; source < partial.ports.size(); ++source) {
        const auto &port = partial.ports[source];
        Require(port.sourcePort == plan.sourcePorts[source] && port.iq.size() == plan.frequencyRaw.size() * 16U,
            "progress source order or IQ capacity differs");
        VerifyProgressPort(port, progress->completedPoints, sweep);
    }
}

void VerifyHeldFrame(const VnaCore::CoreView &pending, const VnaCore::FramePtr &old, const VnaCore::CoreView &other)
{
    Require(pending.frame == old && (!old || old->frameSeq == 1U), "partial data advanced the complete frame");
    Require(!other.sweepProgress && !other.frame, "partial data leaked to another channel");
}

void VerifyCompletedLifecycle(VnaCore::AcquisitionCore &core, const VnaCore::CommandResult &run,
    const VnaCore::SweepPlan &plan, std::uint32_t sweep)
{
    VerifyFrame(core.ReadView().frame, plan, sweep);
    Success(core.Stop(run.runId));
    Success(core.Configure(plan));
    const auto next = Success(core.Start(VnaCore::RunMode::MANUAL));
    Require(next.runId != run.runId && !core.ReadView().sweepProgress,
        "new configuration or run retained old sweep progress");
    Success(core.RemoveChannel(2U));
}

void VerifySlowPrefix(const std::string &library, const std::string &trace, bool fail, bool previous)
{
    const auto gate = previous ? 7U : 3U;
    const auto sweep = previous ? 2U : 1U;
    TestEnvironment environment(trace, fail ? "GET" : "", gate, gate, 731U);
    environment.SetReadDelay(550U);
    const auto plan = Plan({3U, 1U, 4U, 2U}, 256U);
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    if (previous) {
        Success(core.TriggerOnce(run.runId, run.configRevision));
    }
    const auto old = core.ReadView().frame;
    Success(core.CreateChannel(2U));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("GET_BEGIN 128 64 4096", sweep);
    const auto pending = core.ReadView();
    const auto other = core.ReadView(2U);
    // 断言前释放，保证失败时析构也能等到原生调用收尾。
    environment.ReleaseRead();
    const auto result = Await(trigger);
    VerifyProgress(pending, plan, sweep);
    VerifyHeldFrame(pending, old, other);
    Require(!core.ReadView().sweepProgress, "completion or failure left stale sweep progress");
    VerifyProgress(pending, plan, sweep);
    if (fail) {
        Require(!result.ok && core.ReadView().state == VnaCore::CoreState::FAULTED && core.ReadView().frame == old,
            "failed native read did not preserve the last complete frame");
    } else {
        Require(result.ok, result.message);
        VerifyCompletedLifecycle(core, run, plan, sweep);
    }
    static_cast<void>(core.Close());
    Require(!core.ReadView().sweepProgress, "closed core retained partial progress");
    environment.VerifyThreadAndEnvironment();
}

void VerifyFastFallback(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "", 1U, 2U);
    const auto plan = Plan({4U, 2U});
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto begin = std::chrono::steady_clock::now();
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("GET_BEGIN 64 64 2048");
    const auto pending = core.ReadView();
    environment.ReleaseRead();
    Success(trigger);
    Require(!pending.frame && !core.ReadView().sweepProgress, "partial or stale data replaced the complete frame");
    if (std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100)) {
        Require(!pending.sweepProgress, "fast acquisition unnecessarily published a display progress snapshot");
    }
    VerifyFrame(core.ReadView().frame, plan, 1U);
}

void VerifySubsecondSweepDoesNotFlash(const std::string &library, const std::string &trace)
{
    TestEnvironment environment(trace, "", 1U, 2U);
    environment.SetReadDelay(150U);
    const auto plan = Plan({3U, 1U, 4U, 2U});
    VnaCore::AcquisitionCore core(library, VnaCore::DriverApi::TDC1VN10);
    Success(core.Initialize());
    Success(core.Configure(plan));
    const auto run = Success(core.Start(VnaCore::RunMode::MANUAL));
    const auto trigger = core.TriggerOnce(run.runId, run.configRevision);
    environment.AwaitEvent("GET_BEGIN 64 64 4096");
    const auto pending = core.ReadView();
    environment.ReleaseRead();
    Success(trigger);
    Require(!pending.sweepProgress, "subsecond sweep flashed a late progress arrow after the old 100 ms threshold");
    VerifyFrame(core.ReadView().frame, plan, 1U);
}

}  // 匿名命名空间
}  // 命名空间 HardwareBoardTest

int main(int argc, char **argv)
{
    try {
        HardwareBoardTest::Require(argc == 3, "usage: sweep progress test <independent ABI probe> <trace prefix>");
        HardwareBoardTest::VerifySubsecondSweepDoesNotFlash(argv[1], std::string(argv[2]) + "-subsecond");
        HardwareBoardTest::VerifySlowPrefix(argv[1], std::string(argv[2]) + "-first", false, false);
        HardwareBoardTest::VerifySlowPrefix(argv[1], std::string(argv[2]) + "-complete", false, true);
        HardwareBoardTest::VerifySlowPrefix(argv[1], std::string(argv[2]) + "-fault", true, true);
        HardwareBoardTest::VerifyFastFallback(argv[1], std::string(argv[2]) + "-fast");
        std::cout
            << "slow sweep progress passed: native prefix, NaN suffix, immutable complete frame, fault and channels\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
