/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <vector>

#include "support/fixture.h"

namespace {

using namespace VnaCore;
using namespace VnaCoreTest;

void AddSecond(Fixture &fixture)
{
    Require(Await(fixture.core.CreateChannel(2U)).ok, "second channel was not created");
    Require(Await(fixture.core.Configure(MakePlan(1U), 2U)).ok, "second channel was not configured");
}

void TestIndependentManualFrames()
{
    Fixture fixture;
    AddSecond(fixture);
    const auto first = Await(fixture.core.Start(RunMode::MANUAL, 1U));
    const auto second = Await(fixture.core.Start(RunMode::MANUAL, 2U));
    Require(first.ok && second.ok && first.channelId == 1U && second.channelId == 2U,
        "manual runs did not have independent channel identities");
    Require(Await(fixture.core.TriggerOnce(first.runId, first.configRevision, 1U)).ok, "first trigger failed");
    const auto retained = fixture.core.ReadView(1U).frame;
    Require(retained && retained->channelId == 1U && !fixture.core.ReadView(2U).frame,
        "manual trigger acquired or relabelled the other channel");
    Require(Await(fixture.core.TriggerOnce(second.runId, second.configRevision, 2U)).ok, "second trigger failed");
    const auto other = fixture.core.ReadView(2U).frame;
    Require(other && other != retained && other->channelId == 2U && SamePlan(other->plan, MakePlan(1U)) &&
                other->plan.frequencyRaw.size() != retained->plan.frequencyRaw.size(),
        "channels did not acquire distinct physical ranges and point counts");
    const auto changed = Await(fixture.core.Configure(MakePlan(3U), 2U));
    Require(changed.ok && changed.configRevision == second.configRevision + 1U &&
                fixture.core.ReadView(1U).configRevision == first.configRevision &&
                fixture.core.ReadView(1U).frame == retained,
        "configuration changed another channel revision or immutable cache");
    Require(!Await(fixture.core.TriggerOnce(second.runId, second.configRevision, 2U)).ok,
        "stale channel configuration was accepted");
    Require(Await(fixture.core.TriggerOnce(first.runId, first.configRevision, 1U)).ok &&
                SamePlan(fixture.core.ReadView(1U).frame->plan, MakePlan()),
        "returning to the first channel did not restore its physical plan");
    std::lock_guard<std::mutex> guard(fixture.device->mutex_);
    const auto &events = fixture.device->planEvents_;
    for (std::size_t index = 1U; index < events.size(); ++index) {
        if (events[index].first) {
            Require(SamePlan(events[index - 1U].second, events[index].second),
                "device acquisition was not preceded by the matching physical plan");
        }
    }
    Require(!fixture.device->wrongThread_, "multiple worker threads accessed the device");
}

void TestRoundRobinAndDeleteBarrier()
{
    Fixture fixture;
    AddSecond(fixture);
    const auto first = Await(fixture.core.Start(RunMode::CONTINUOUS, 1U));
    const auto second = Await(fixture.core.Start(RunMode::CONTINUOUS, 2U));
    Require(first.ok && second.ok, "continuous channels did not start");
    {
        std::unique_lock<std::mutex> lock(fixture.device->mutex_);
        Require(fixture.device->cv_.wait_for(lock, std::chrono::seconds(3),
                    [&fixture] {
                        std::size_t frames = 0U;
                        for (const auto &event : fixture.device->planEvents_) {
                            frames += event.first ? 1U : 0U;
                        }
                        return frames >= 8U;
                    }),
            "both continuous channels did not make progress");
    }
    Require(Await(fixture.core.Stop(first.runId, 1U)).ok && Await(fixture.core.Stop(second.runId, 2U)).ok,
        "continuous Stop barrier failed");
    const auto retained = fixture.core.ReadView(1U).frame;
    const auto other = fixture.core.ReadView(2U).frame;
    Require(retained && other && retained->frameSeq >= 3U && other->frameSeq >= 3U,
        "continuous round robin starved one channel");
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        std::vector<SweepPlan> frames;
        for (const auto &event : fixture.device->planEvents_) {
            if (event.first) {
                frames.push_back(event.second);
            }
        }
        // 开始和停止命令处允许单通道帧，两个运行同时存在的中间区间必须逐完整帧交替。
        for (std::size_t index = 2U; index + 1U < 8U; ++index) {
            Require(!SamePlan(frames[index - 1U], frames[index]), "continuous plans did not alternate fairly");
        }
    }
    const auto restarted = Await(fixture.core.Start(RunMode::MANUAL, 2U));
    fixture.device->Arm();
    const auto trigger = fixture.core.TriggerOnce(restarted.runId, restarted.configRevision, 2U);
    fixture.device->WaitEntered();
    const auto removed = fixture.core.RemoveChannel(2U);
    Require(removed.wait_for(std::chrono::milliseconds(40)) == std::future_status::timeout,
        "channel deletion crossed an unfinished physical frame");
    fixture.device->ReleaseGate();
    Require(Await(trigger).ok && Await(removed).ok && fixture.core.ReadView(2U).state == CoreState::UNINITIALIZED &&
                !fixture.core.ReadView(2U).error.empty(),
        "deletion did not safely remove the completed channel");
    Require(fixture.core.ReadView(1U).frame == retained && !Await(fixture.core.RemoveChannel(1U)).ok,
        "deletion affected another immutable cache or removed the last channel");
    Require(other->channelId == 2U && SamePlan(other->plan, MakePlan(1U)), "retained deleted frame was mutated");
    Require(Await(fixture.core.CreateChannel(2U)).ok && Await(fixture.core.Configure(MakePlan(1U), 2U)).ok,
        "deleted channel ID could not be safely recreated");
    const auto recreated = Await(fixture.core.Start(RunMode::MANUAL, 2U));
    Require(recreated.ok && recreated.runId > restarted.runId && recreated.configRevision > restarted.configRevision &&
                !Await(fixture.core.TriggerOnce(restarted.runId, restarted.configRevision, 2U)).ok,
        "recreated channel accepted a stale run or configuration identity");
}

void TestGlobalFaultAndRecovery()
{
    Fixture fixture;
    AddSecond(fixture);
    const auto first = Await(fixture.core.Start(RunMode::MANUAL, 1U));
    const auto second = Await(fixture.core.Start(RunMode::MANUAL, 2U));
    Require(Await(fixture.core.TriggerOnce(first.runId, first.configRevision, 1U)).ok &&
                Await(fixture.core.TriggerOnce(second.runId, second.configRevision, 2U)).ok,
        "fault scenario baseline frames failed");
    const auto retained = fixture.core.ReadView(1U).frame;
    const auto other = fixture.core.ReadView(2U).frame;
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        fixture.device->failApply_ = true;
    }
    Require(!Await(fixture.core.Configure(MakePlan(4U), 2U)).ok, "partial physical configuration failure was hidden");
    Require(fixture.core.ReadView(1U).state == CoreState::FAULTED &&
                fixture.core.ReadView(2U).state == CoreState::FAULTED && fixture.core.ReadView(1U).frame == retained &&
                fixture.core.ReadView(2U).frame == other &&
                fixture.core.ReadView(2U).configRevision == second.configRevision,
        "device fault did not pause every channel while retaining successful revisions and frames");
    Require(!Await(fixture.core.TriggerOnce(first.runId, first.configRevision, 1U)).ok,
        "another channel continued against a faulted physical device");
    Require(Await(fixture.core.Recover()).ok && fixture.core.ReadView(1U).state == CoreState::READY &&
                fixture.core.ReadView(2U).state == CoreState::READY &&
                fixture.core.ReadView(1U).configRevision == first.configRevision + 1U &&
                fixture.core.ReadView(2U).configRevision == second.configRevision + 1U &&
                fixture.core.ReadView(1U).frame == retained && fixture.core.ReadView(2U).frame == other,
        "global recovery did not revalidate and version each channel without auto restarting");
}

void TestBudgetAndInactiveValidation()
{
    Fixture fixture;
    Require(!Await(fixture.core.CreateChannel(0U)).ok && !Await(fixture.core.CreateChannel(1U)).ok &&
                !Await(fixture.core.Configure(MakePlan(), 2U)).ok,
        "invalid, duplicate or nonexistent channel was accepted");
    AddSecond(fixture);
    const auto original = fixture.core.ReadView(1U);
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        fixture.device->failPrepare_ = true;
    }
    const auto rejected = Await(fixture.core.Configure(MakePlan(3U), 2U));
    Require(!rejected.ok && fixture.core.ReadView(2U).configRevision == 1U &&
                fixture.core.ReadView(1U).configRevision == original.configRevision &&
                fixture.core.ReadView(1U).state == CoreState::READY,
        "inactive configuration bypassed device validation or committed a failed revision");
    for (unsigned channelId = 3U; channelId <= 16U; ++channelId) {
        Require(Await(fixture.core.CreateChannel(channelId)).ok, "channel budget rejected a valid live channel");
    }
    Require(!Await(fixture.core.CreateChannel(17U)).ok && Await(fixture.core.RemoveChannel(2U)).ok &&
                Await(fixture.core.CreateChannel(17U)).ok,
        "live channel budget did not cap at 16 or reclaim a removed slot");
    Require(Await(fixture.core.RemoveChannel(1U)).ok && !fixture.core.ReadView(1U).error.empty(),
        "default channel was incorrectly mandatory after another channel exists");
}

void TestCommandFloodDoesNotStarveFrames()
{
    Fixture fixture;
    AddSecond(fixture);
    fixture.device->Arm();
    const auto first = Await(fixture.core.Start(RunMode::CONTINUOUS, 1U));
    fixture.device->WaitEntered();
    const auto secondTicket = fixture.core.Start(RunMode::CONTINUOUS, 2U);
    {
        std::lock_guard<std::mutex> guard(fixture.device->mutex_);
        fixture.device->applyDelay_ = std::chrono::milliseconds(3);
    }
    std::vector<Ticket> commands;
    for (std::size_t index = 0U; index < 60U; ++index) {
        commands.push_back(fixture.core.Configure(MakePlan(), 1U));
    }
    fixture.device->ReleaseGate();
    const auto second = Await(secondTicket);
    Require(second.ok, "second continuous channel did not start before command flood");
    Require(commands.back().wait_for(std::chrono::milliseconds(60)) == std::future_status::timeout &&
                fixture.core.ReadView(1U).frame && fixture.core.ReadView(2U).frame,
        "continuous scanning was starved while the FIFO command queue remained nonempty");
    for (const auto &ticket : commands) {
        Require(Await(ticket).ok, "queued configuration did not preserve global FIFO");
    }
    Require(Await(fixture.core.Stop(first.runId, 1U)).ok && Await(fixture.core.Stop(second.runId, 2U)).ok,
        "flooded channels did not stop independently");
}

}  // 匿名命名空间

int main()
{
    try {
        TestIndependentManualFrames();
        TestRoundRobinAndDeleteBarrier();
        TestGlobalFaultAndRecovery();
        TestBudgetAndInactiveValidation();
        TestCommandFloodDoesNotStarveFrames();
        std::cout << "多通道物理采集：5 组场景通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
