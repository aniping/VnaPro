/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_CORE_TEST_CONTROLLED_DEVICE_H
#define VNAPRO_CORE_TEST_CONTROLLED_DEVICE_H

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "concurrency_assertions.h"
#include "device/plan_preparation_error.h"
#include "vna_core/device_session.h"

namespace VnaCoreTest {

class ControlledDevice final : public VnaCore::Detail::DeviceSession {
public:
    // Gate state and the worker identity remain bound to this instance.
    ControlledDevice() = default;
    ControlledDevice(const ControlledDevice &) = delete;
    ControlledDevice &operator=(const ControlledDevice &) = delete;
    ControlledDevice(ControlledDevice &&) = delete;
    ControlledDevice &operator=(ControlledDevice &&) = delete;
    ~ControlledDevice() override = default;
    // Returns the fixed controlled identity without calling a native driver.
    std::string Initialize() override;

    // Applies the frozen plan or blocks/fails at a gate controlled by this test.
    void ApplyPlan(const VnaCore::SweepPlan &plan) override;

    // Begins a controlled run without native RF output.
    void EnableRf() override;

    // Returns a complete deterministic IQ vector after any armed gate is released.
    std::vector<double> AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t source) override;

    // Records or completes controlled cleanup; injected failures remain observable.
    void Shutdown() override;

    // Resets and arms the next acquisition barrier; optional failure follows gate release.
    void Arm();

    // Waits at most three seconds for the worker to reach the armed gate.
    void WaitEntered();

    // Releases the current acquisition gate and wakes its waiting worker.
    void ReleaseGate();

    std::mutex mutex_;
    std::condition_variable cv_;
    bool blockNext_ = false;
    bool entered_ = false;
    bool release_ = false;
    bool failApply_ = false;
    bool failPrepare_ = false;
    bool failShutdown_ = false;
    std::size_t failAcquireAt_ = 0U;
    std::size_t shortAcquireAt_ = 0U;
    std::size_t acquisitions_ = 0U;
    std::uint64_t acquisitionFrame_ = 0U;
    std::thread::id deviceThread_;
    bool wrongThread_ = false;
    bool shutdownCalled_ = false;
    VnaCore::SweepPlan applied_;

private:
    void RecordThread();
};

}  // namespace VnaCoreTest

#endif
