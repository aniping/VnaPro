/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CONCURRENT_STRESS_SCENARIO_H
#define VNAPRO_CONCURRENT_STRESS_SCENARIO_H

#include <atomic>
#include <future>
#include <mutex>
#include <utility>
#include <vector>

#include "fixture.h"

namespace VnaCoreTest {
class ConcurrentStressScenario final {
public:
    // Creates one running device and freezes a baseline frame for all readers.
    ConcurrentStressScenario();
    ConcurrentStressScenario(const ConcurrentStressScenario &) = delete;
    ConcurrentStressScenario &operator=(const ConcurrentStressScenario &) = delete;
    ConcurrentStressScenario(ConcurrentStressScenario &&) = delete;
    ConcurrentStressScenario &operator=(ConcurrentStressScenario &&) = delete;
    // Drains every async worker before propagating its first error.
    void Run();

private:
    void SubmitWorker(std::size_t submitter);
    void ReadWorker();
    void CheckSnapshots(const std::vector<std::pair<VnaCore::FramePtr, VnaCore::MeasurementFrame>> &snapshots);
    void CheckResults();
    static void CollectWorkers(std::vector<std::future<void>> &workers, std::exception_ptr &workerError);

    Fixture fixture_;
    VnaCore::CommandResult started_;
    VnaCore::FramePtr retained_;
    VnaCore::MeasurementFrame baseline_;
    std::promise<void> startWorkers_;
    std::shared_future<void> startSignal_ = startWorkers_.get_future().share();
    std::atomic<bool> finished_{false};
    std::atomic<std::size_t> readersReady_{0U};
    std::atomic<std::size_t> checkedViews_{0U};
    std::atomic<std::size_t> completedTickets_{0U};
    std::atomic<std::size_t> successfulTriggers_{0U};
    std::atomic<std::size_t> rejectedTriggers_{0U};
    std::mutex plansMutex_;
    std::vector<std::pair<std::uint64_t, VnaCore::SweepPlan>> committedPlans_;
};
}  // namespace VnaCoreTest
#endif
