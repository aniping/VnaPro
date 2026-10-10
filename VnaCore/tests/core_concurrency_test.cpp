/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "support/concurrency_assertions.h"
#include "support/concurrent_stress_scenario.h"
#include "support/fixture.h"
#include "support/release_gate_on_exit.h"
#include "support/worker_start_on_exit.h"
#include "vna_core/acquisition_core.h"

namespace {
using namespace vna_core;
using namespace vna_core_test;
using namespace std::chrono;

void TestStopFifo()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(started.ok, "manual start failed");
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "initial frame failed");
    const auto old = f.core.ReadView().frame;
    f.device->Arm();
    const auto inFlight = f.core.TriggerOnce(started.runId, started.configRevision);
    f.device->WaitEntered();
    const auto priorTrigger = f.core.TriggerOnce(started.runId, started.configRevision);
    const auto stop = f.core.Stop(started.runId);
    const auto laterTrigger = f.core.TriggerOnce(started.runId, started.configRevision);
    Require(stop.wait_for(milliseconds(80)) == std::future_status::timeout, "Stop passed an unfinished frame");
    Require(f.core.ReadView().frame == old, "partial frame became visible");
    f.device->ReleaseGate();
    Require(Await(inFlight).ok && Await(priorTrigger).ok, "prior FIFO triggers did not execute");
    const auto stopped = Await(stop);
    Require(stopped.ok && stopped.frameSeq == 3U, "Stop did not cover preceding frames");
    Require(!Await(laterTrigger).ok, "trigger behind Stop succeeded");
    Require(f.core.ReadView().state == CoreState::READY, "Stop did not publish Ready");
    Require(
        f.core.ReadView().frame->ports.size() == 2U && old->frameSeq == 1U, "complete immutable frame was modified");
    Require(Await(f.core.Stop(started.runId)).ok, "matching idle Stop was not idempotent");
    const auto restarted = Await(f.core.Start(RunMode::MANUAL));
    Require(restarted.runId == started.runId + 1U, "run identity did not advance");
    Require(!Await(f.core.Stop(started.runId)).ok, "stale Stop affected a new run");
}

void TestContinuousStop()
{
    Fixture f;
    f.device->Arm();
    const auto started = Await(f.core.Start(RunMode::CONTINUOUS));
    f.device->WaitEntered();
    const auto stop = f.core.Stop(started.runId);
    Require(stop.wait_for(milliseconds(80)) == std::future_status::timeout, "continuous Stop passed device I/O");
    f.device->ReleaseGate();
    Require(Await(stop).ok, "continuous Stop failed");
    const auto view = f.core.ReadView();
    Require(view.state == CoreState::READY && view.frame && view.frame->ports.size() == 2U,
        "continuous frame was not fully committed before Stop");
    std::unique_lock<std::mutex> lock(f.device->mutex_);
    const auto acquisitions = f.device->acquisitions_;
    Require(!f.device->cv_.wait_for(lock, milliseconds(80),
                [&f, acquisitions] {
                    return f.device->acquisitions_ != acquisitions;
                }),
        "old run acquired another port after Stop confirmation");
}

void TestConfigurationFailureAndRecover()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    Require(Await(f.core.Stop(started.runId)).ok, "baseline stop failed");
    const auto old = f.core.ReadView();
    auto replacement = MakePlan();
    replacement.frequencyRaw[0] = 77U;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        f.device->failApply_ = true;
    }
    Require(!Await(f.core.Configure(replacement)).ok, "partial configuration failure was hidden");
    const auto failed = f.core.ReadView();
    Require(
        failed.state == CoreState::FAULTED && failed.configRevision == old.configRevision && failed.frame == old.frame,
        "failed configuration committed a revision or replaced the frame");
    Require(Await(f.core.Stop(started.runId)).ok && f.core.ReadView().state == CoreState::FAULTED,
        "matching Stop erased a persistent fault");
    Require(!Await(f.core.Start(RunMode::MANUAL)).ok, "Faulted core restarted without recovery");
    const auto recovered = Await(f.core.Recover());
    Require(recovered.ok && recovered.configRevision == old.configRevision + 1U,
        "recover did not completely restore and version the last successful plan");
    Require(f.device->applied_.frequencyRaw == MakePlan().frequencyRaw,
        "recover restored the failed candidate instead of the last successful plan");
    Require(f.core.ReadView().state == CoreState::READY && f.core.ReadView().error.empty(),
        "recover resumed automatically or retained the old fault");
}

void TestAcquisitionFailureDoesNotPublish()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    const auto old = f.core.ReadView().frame;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        f.device->failAcquireAt_ = f.device->acquisitions_ + 2U;
    }
    Require(!Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok,
        "second-port failure did not fail its trigger ticket");
    Require(f.core.ReadView().frame == old && f.core.ReadView().state == CoreState::FAULTED,
        "second-port failure published a partial frame");
}

void TestPreparationFailurePreservesConfiguration()
{
    Fixture f;
    const auto old = f.core.ReadView();
    auto candidate = MakePlan();
    candidate.frequencyRaw[0] = 77U;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        f.device->failPrepare_ = true;
    }
    Require(!Await(f.core.Configure(candidate)).ok, "preparation failure was hidden");
    Require(f.core.ReadView().state == CoreState::READY && f.core.ReadView().configRevision == old.configRevision &&
                f.core.ReadView().error.empty(),
        "preparation failure invalidated a previously valid configuration");
    Require(f.device->applied_.frequencyRaw == MakePlan().frequencyRaw, "preparation failure mutated the device");
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(started.ok && Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok,
        "previous valid configuration could not be used after preparation failure");
    Require(!Await(f.core.Stop(0U)).ok, "zero run identifier was accepted");
}

void TestCloseReservedAndTicketsTerminate()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    f.device->Arm();
    const auto inFlight = f.core.TriggerOnce(started.runId, started.configRevision);
    f.device->WaitEntered();
    std::vector<Ticket> tickets;
    std::size_t rejected = 0U;
    for (std::size_t index = 0U; index < 80U; ++index) {
        const auto ticket = f.core.TriggerOnce(started.runId, started.configRevision);
        if (ticket.wait_for(milliseconds(0)) == std::future_status::ready) {
            Require(!ticket.get().ok, "full queue rejection pretended success");
            ++rejected;
        }
        tickets.push_back(ticket);
    }
    Require(rejected == 16U, "normal queue capacity was not enforced");
    auto closing = std::async(std::launch::async, [core = &f.core] {
        return core->Close();
    });
    ReleaseGateOnExit releaseOnExit{*f.device};
    bool admissionClosed = false;
    const auto deadline = steady_clock::now() + seconds(3);
    while (!admissionClosed && steady_clock::now() < deadline) {
        const auto answer = Await(f.core.Initialize());
        admissionClosed = answer.message.find("closing") != std::string::npos;
        std::this_thread::yield();
    }
    Require(admissionClosed, "Close did not shut admission while the queue was full");
    Require(closing.wait_for(milliseconds(80)) == std::future_status::timeout,
        "Close returned without waiting for in-flight device I/O");
    f.device->ReleaseGate();
    Require(Await(inFlight).ok, "in-flight ticket was abandoned by Close");
    std::size_t successfulQueued = 0U;
    for (const auto &ticket : tickets) {
        successfulQueued += Await(ticket).ok ? 1U : 0U;
    }
    Require(successfulQueued == 64U, "Close abandoned an accepted queued trigger");
    Require(closing.get().ok && f.core.ReadView().state == CoreState::CLOSED,
        "reserved Close entry failed to drain and join");
    Require(
        f.device->shutdownCalled_ && !f.device->wrongThread_ && f.device->deviceThread_ != std::this_thread::get_id(),
        "device I/O escaped its single worker");
    Require(!Await(f.core.Configure(MakePlan())).ok, "closed core accepted new work");
}

void TestOwnedInputAndShutdownError()
{
    Fixture f;
    auto input = MakePlan();
    const auto configured = f.core.Configure(input);
    input.frequencyRaw[0] = 42U;
    input.tableData.clear();
    Require(Await(configured).ok, "owned input failed to apply");
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "owned input scan failed");
    Require(
        f.core.ReadView().frame->plan.frequencyRaw[0] == UINT64_C(1000000), "caller mutation changed the frozen plan");
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        f.device->failShutdown_ = true;
    }
    Require(!f.core.Close().ok && f.core.ReadView().state == CoreState::CLOSED,
        "RF shutdown error prevented terminal Closed state");
}

void TestConfigurationSwitchAtFrameBoundary()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "baseline frame failed");
    const auto baseline = f.core.ReadView().frame;
    f.device->Arm();
    ReleaseGateOnExit releaseOnExit{*f.device};
    const auto inFlight = f.core.TriggerOnce(started.runId, started.configRevision);
    f.device->WaitEntered();
    const auto replacement = MakePlan(1U);
    const auto configured = f.core.Configure(replacement);
    Require(configured.wait_for(milliseconds(80)) == std::future_status::timeout,
        "configuration crossed an unfinished multi-source frame");
    Require(f.core.ReadView().frame == baseline, "blocked frame was published before all sources completed");
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        Require(SamePlan(f.device->applied_, MakePlan()), "configuration mutated hardware during an active frame");
    }
    f.device->ReleaseGate();
    Require(Await(inFlight).ok, "old configuration frame failed during replacement");
    const auto changed = Await(configured);
    Require(changed.ok && changed.configRevision == started.configRevision + 1U,
        "successful replacement was not committed exactly once");
    const auto oldFrame = f.core.ReadView().frame;
    Require(oldFrame->frameSeq == 2U && oldFrame->configRevision == started.configRevision &&
                SamePlan(oldFrame->plan, MakePlan()),
        "old frame mixed replacement metadata or frequencies");
    RequireFrameContent(*oldFrame);
    Require(!Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok,
        "old-revision trigger was accepted after successful replacement");
    Require(Await(f.core.TriggerOnce(started.runId, changed.configRevision)).ok, "new configuration frame failed");
    const auto newFrame = f.core.ReadView().frame;
    Require(newFrame->runId == started.runId && newFrame->frameSeq == 3U &&
                newFrame->configRevision == changed.configRevision && SamePlan(newFrame->plan, replacement),
        "next frame did not fully adopt the new configuration");
    RequireFrameContent(*newFrame);
}

void TestRetainedFrameDeepImmutability()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "retained baseline failed");
    const auto retained = f.core.ReadView().frame;
    const MeasurementFrame snapshot = *retained;
    RequireFrameContent(*retained);
    for (std::uint32_t variant = 1U; variant <= 5U; ++variant) {
        const auto plan = MakePlan(variant);
        const auto configured = Await(f.core.Configure(plan));
        Require(configured.ok && Await(f.core.TriggerOnce(started.runId, configured.configRevision)).ok,
            "replacement scan failed in immutability test");
        const auto fresh = f.core.ReadView().frame;
        Require(fresh != retained && SamePlan(fresh->plan, plan), "replacement reused the retained frame");
        RequireFrameContent(*fresh);
        RequireUnchanged(*retained, snapshot);
    }
    Require(f.core.Close().ok, "close failed with a retained frame");
    RequireUnchanged(*retained, snapshot);
}

void TestShortIqDoesNotPublish()
{
    Fixture f;
    const auto started = Await(f.core.Start(RunMode::MANUAL));
    Require(Await(f.core.TriggerOnce(started.runId, started.configRevision)).ok, "short-IQ baseline failed");
    const auto retained = f.core.ReadView().frame;
    const MeasurementFrame snapshot = *retained;
    {
        std::lock_guard<std::mutex> lock(f.device->mutex_);
        f.device->shortAcquireAt_ = f.device->acquisitions_ + 2U;
    }
    const auto failed = Await(f.core.TriggerOnce(started.runId, started.configRevision));
    const auto view = f.core.ReadView();
    Require(!failed.ok && failed.message.find("incomplete IQ") != std::string::npos &&
                view.state == CoreState::FAULTED && view.frame == retained && failed.frameSeq == snapshot.frameSeq,
        "short second-source IQ published a partial frame or escaped fault handling");
    RequireUnchanged(*retained, snapshot);
}

void TestConcurrentSubmittersAndReaders()
{
    ConcurrentStressScenario scenario;
    scenario.Run();
}

}  // namespace

int main()
{
    try {
        TestStopFifo();
        TestContinuousStop();
        TestConfigurationFailureAndRecover();
        TestAcquisitionFailureDoesNotPublish();
        TestPreparationFailurePreservesConfiguration();
        TestCloseReservedAndTicketsTerminate();
        TestOwnedInputAndShutdownError();
        TestConfigurationSwitchAtFrameBoundary();
        TestRetainedFrameDeepImmutability();
        TestShortIqDoesNotPublish();
        TestConcurrentSubmittersAndReaders();
        std::cout << "core concurrency: 11 scenarios passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
