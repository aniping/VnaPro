/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "concurrent_stress_scenario.h"

#include <iostream>
#include <thread>

#include "concurrency_assertions.h"
#include "worker_start_on_exit.h"

namespace VnaCoreTest {
namespace {
constexpr std::size_t SUBMITTER_COUNT = 3U;
constexpr std::size_t READER_COUNT = 2U;
}  // 匿名命名空间

using namespace VnaCore;
using namespace std::chrono;

ConcurrentStressScenario::ConcurrentStressScenario()
{
    started_ = Await(fixture_.core.Start(RunMode::MANUAL));
    Require(Await(fixture_.core.TriggerOnce(started_.runId, started_.configRevision)).ok, "stress baseline failed");
    retained_ = fixture_.core.ReadView().frame;
    baseline_ = *retained_;
    committedPlans_.emplace_back(started_.configRevision, MakePlan());
}

void ConcurrentStressScenario::SubmitWorker(std::size_t submitter)
{
    startSignal_.wait();
    const auto readyDeadline = steady_clock::now() + seconds(3);
    while (readersReady_.load() != READER_COUNT) {
        Require(steady_clock::now() < readyDeadline, "stress readers never became ready");
        std::this_thread::yield();
    }
    for (std::size_t index = 0U; index < 48U; ++index) {
        if (index % 12U == submitter) {
            const auto plan = MakePlan(static_cast<std::uint32_t>(1U + 48U * submitter + index));
            const auto changed = Await(fixture_.core.Configure(plan));
            ++completedTickets_;
            Require(changed.ok, "concurrent valid configuration failed");
            std::lock_guard<std::mutex> lock(plansMutex_);
            committedPlans_.emplace_back(changed.configRevision, plan);
        } else {
            const auto view = fixture_.core.ReadView();
            const auto triggered = Await(fixture_.core.TriggerOnce(view.runId, view.configRevision));
            ++completedTickets_;
            if (triggered.ok) {
                ++successfulTriggers_;
            } else {
                ++rejectedTriggers_;
                Require(triggered.message == "trigger requires the current manual run and configuration",
                    "concurrent trigger failed for a reason other than a superseded revision");
            }
        }
    }
}

void ConcurrentStressScenario::ReadWorker()
{
    startSignal_.wait();
    std::vector<std::pair<FramePtr, MeasurementFrame>> snapshots{{retained_, baseline_}};
    ++readersReady_;
    std::size_t checks = 0U;
    std::uint64_t previousFrame = 0U;
    std::uint64_t previousRevision = 0U;
    const auto deadline = steady_clock::now() + seconds(5);
    while (!finished_.load() || checks < 256U) {
        Require(steady_clock::now() < deadline, "concurrent read stress exceeded its bound");
        const auto view = fixture_.core.ReadView();
        Require(view.state == CoreState::RUNNING && view.error.empty() && view.frame && view.runId == started_.runId &&
                    view.frame->runId == view.runId && view.frame->configRevision <= view.configRevision &&
                    view.frame->frameSeq >= previousFrame && view.configRevision >= previousRevision,
            "readView exposed inconsistent or regressing published metadata");
        RequireFrameContent(*view.frame);
        RequireUnchanged(*retained_, baseline_);
        if (snapshots.back().first != view.frame) {
            snapshots.emplace_back(view.frame, *view.frame);
        }
        previousFrame = view.frame->frameSeq;
        previousRevision = view.configRevision;
        ++checks;
        ++checkedViews_;
        std::this_thread::yield();
    }
    CheckSnapshots(snapshots);
}

void ConcurrentStressScenario::CheckSnapshots(const std::vector<std::pair<FramePtr, MeasurementFrame>> &snapshots)
{
    std::lock_guard<std::mutex> lock(plansMutex_);
    for (const auto &snapshot : snapshots) {
        RequireUnchanged(*snapshot.first, snapshot.second);
        bool matchedRevision = false;
        for (const auto &committed : committedPlans_) {
            if (snapshot.first->configRevision == committed.first) {
                matchedRevision = SamePlan(snapshot.first->plan, committed.second);
                break;
            }
        }
        Require(matchedRevision, "frame configuration did not match its committed revision");
    }
}

void ConcurrentStressScenario::CollectWorkers(std::vector<std::future<void>> &workers, std::exception_ptr &workerError)
{
    for (auto &worker : workers) {
        try {
            worker.get();
        } catch (...) {
            if (!workerError) {
                workerError = std::current_exception();
            }
        }
    }
}

void ConcurrentStressScenario::Run()
{
    std::vector<std::future<void>> submitters;
    std::vector<std::future<void>> readers;
    // 在异步工作线程开始等待释放信号前，预先分配结果句柄槽位。
    submitters.reserve(SUBMITTER_COUNT);
    readers.reserve(READER_COUNT);
    WorkerStartOnExit releaseWorkersOnExit{startWorkers_, finished_};
    for (std::size_t submitter = 0U; submitter < SUBMITTER_COUNT; ++submitter) {
        submitters.push_back(std::async(std::launch::async, [this, submitter] {
            SubmitWorker(submitter);
        }));
    }
    for (std::size_t reader = 0U; reader < READER_COUNT; ++reader) {
        readers.push_back(std::async(std::launch::async, [this] {
            ReadWorker();
        }));
    }
    startWorkers_.set_value();
    releaseWorkersOnExit.pending = false;
    std::exception_ptr workerError;
    CollectWorkers(submitters, workerError);
    finished_.store(true);
    CollectWorkers(readers, workerError);
    if (workerError) {
        std::rethrow_exception(workerError);
    }
    CheckResults();
}

void ConcurrentStressScenario::CheckResults()
{
    Require(completedTickets_.load() == 144U && committedPlans_.size() == 13U && checkedViews_.load() >= 512U,
        "stress did not complete every bounded submission and reader check");
    Require(successfulTriggers_.load() > 0U && successfulTriggers_.load() + rejectedTriggers_.load() == 132U &&
                fixture_.core.ReadView().frame->frameSeq == 1U + successfulTriggers_.load(),
        "stress lost or duplicated a successfully triggered frame");
    Require(Await(fixture_.core.Stop(started_.runId)).ok && fixture_.core.Close().ok,
        "stress did not stop and close cleanly");
    RequireUnchanged(*retained_, baseline_);
    {
        std::lock_guard<std::mutex> lock(fixture_.device->mutex_);
        Require(!fixture_.device->wrongThread_ && fixture_.device->shutdownCalled_ &&
                    fixture_.device->deviceThread_ != std::this_thread::get_id(),
            "concurrent clients caused device I/O to escape its single worker");
    }
    std::cout << "stress: 144 tickets, " << successfulTriggers_.load() << " frames, " << rejectedTriggers_.load()
              << " superseded triggers, " << checkedViews_.load() << " consistent views\n";
}
}  // 命名空间 VnaCoreTest
