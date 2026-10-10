/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVICE_CONTROLLED_DEVICE_H
#define VNAPRO_SERVICE_CONTROLLED_DEVICE_H

#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "vna_core/device_session.h"

namespace VnaServiceTest {
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
    void ApplyPlan(const VnaCore::SweepPlan &) override;

    // A controlled run requires no native RF operation and cannot fail during arming.
    void EnableRf() noexcept override;

    // This device owns no native RF or loader handle, so shutdown requires no cleanup.
    void Shutdown() noexcept override;

    // Returns a complete deterministic IQ vector after any armed gate is released.
    std::vector<double> AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t) override;

    // Toggles deliberate NaN/infinity injection while holding the device mutex.
    void SetInvalidSamples(bool value);

    // Rejects the next plan before device mutation, leaving a bootstrap attempt retryable.
    void FailNextPlanPreparation();

    // Reads acquisition-gate entry under the device mutex.
    bool HasEntered() const;

    // Reads configuration-gate entry under the device mutex.
    bool HasPlanEntered() const;

    // Resets and arms the next configuration barrier.
    void ArmPlan();

    // Resets and arms the next acquisition barrier; optional failure follows gate release.
    void Arm(bool fail = false);

    // Releases configuration and acquisition gates so fixture cleanup cannot hang.
    void Release();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool blocked_ = false;
    bool entered_ = false;
    bool released_ = false;
    bool failAcquire_ = false;
    bool invalidSamples_ = false;
    bool blockPlan_ = false;
    bool planEntered_ = false;
    bool planReleased_ = false;
    bool failPlanPreparation_ = false;
};
}  // namespace VnaServiceTest
#endif
