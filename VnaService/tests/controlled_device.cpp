/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "controlled_device.h"

#include "../../VnaCore/src/device/plan_preparation_error.h"

namespace VnaServiceTest {

std::string ControlledDevice::Initialize()
{
    return "VnaPro,NativeSimulator,TEST,1.0";
}

void ControlledDevice::ApplyPlan(const VnaCore::SweepPlan &)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (failPlanPreparation_) {
        failPlanPreparation_ = false;
        throw VnaCore::Detail::PlanPreparationError();
    }
    if (blockPlan_) {
        planEntered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] {
            return planReleased_;
        });
    }
}

void ControlledDevice::EnableRf() noexcept
{
}

void ControlledDevice::Shutdown() noexcept
{
}

std::vector<double> ControlledDevice::AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (blocked_) {
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] {
            return released_;
        });
    }
    if (failAcquire_) {
        throw std::runtime_error("controlled acquisition failure");
    }
    std::vector<double> iq(plan.frequencyRaw.size() * 16U, 1.11);
    if (invalidSamples_ && plan.frequencyRaw.size() >= 3U) {
        iq[16U] = std::numeric_limits<double>::quiet_NaN();
        iq[33U] = std::numeric_limits<double>::infinity();
    }
    return iq;
}

void ControlledDevice::SetInvalidSamples(bool value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    invalidSamples_ = value;
}

void ControlledDevice::FailNextPlanPreparation()
{
    std::lock_guard<std::mutex> lock(mutex_);
    failPlanPreparation_ = true;
}

bool ControlledDevice::HasEntered() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
}

bool ControlledDevice::HasPlanEntered() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return planEntered_;
}

void ControlledDevice::ArmPlan()
{
    std::lock_guard<std::mutex> lock(mutex_);
    blockPlan_ = true;
    planEntered_ = false;
    planReleased_ = false;
}

void ControlledDevice::Arm(bool fail)
{
    std::lock_guard<std::mutex> lock(mutex_);
    blocked_ = true;
    entered_ = false;
    released_ = false;
    failAcquire_ = fail;
}

void ControlledDevice::Release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    blocked_ = false;
    planReleased_ = true;
    blockPlan_ = false;
    cv_.notify_all();
}

}  // namespace VnaServiceTest
