/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "controlled_device.h"

#include <utility>

#include "../../VnaCore/src/device/plan_preparation_error.h"
#include "vna_core/frame_layout.h"

namespace VnaServiceTest {

ControlledDevice::ControlledDevice(std::string identity) : identity_(std::move(identity))
{
}

std::string ControlledDevice::Initialize()
{
    return identity_;
}

bool ControlledDevice::ManualSweepTimeSupported() const noexcept
{
    return true;
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

std::vector<VnaCore::PortFrame> ControlledDevice::AcquireSweep(const VnaCore::SweepPlan &plan)
{
    std::vector<VnaCore::PortFrame> ports;
    for (const auto source : plan.sourcePorts) {
        VnaCore::PortFrame port;
        port.sourcePort = source;
        port.iq = AcquirePort(plan);
        ports.push_back(std::move(port));
    }
    return ports;
}

std::vector<double> ControlledDevice::AcquirePort(const VnaCore::SweepPlan &plan)
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
    std::vector<double> iq = samples_.empty()
                                 ? std::vector<double>(plan.frequencyRaw.size() * VnaCore::IQ_VALUES_PER_POINT, 1.11)
                                 : samples_;
    if (samples_.size() == VnaCore::IQ_VALUES_PER_POINT) {
        iq.clear();
        iq.reserve(plan.frequencyRaw.size() * VnaCore::IQ_VALUES_PER_POINT);
        for (std::size_t point = 0; point < plan.frequencyRaw.size(); ++point) {
            iq.insert(iq.end(), samples_.begin(), samples_.end());
        }
    }
    if (invalidSamples_ && iq.size() >= 3U * VnaCore::IQ_VALUES_PER_POINT) {
        iq[VnaCore::IQ_VALUES_PER_POINT] = std::numeric_limits<double>::quiet_NaN();
        iq[2U * VnaCore::IQ_VALUES_PER_POINT + 1U] = std::numeric_limits<double>::infinity();
    }
    return iq;
}

void ControlledDevice::SetInvalidSamples(bool value)
{
    std::lock_guard<std::mutex> lock(mutex_);
    invalidSamples_ = value;
}

void ControlledDevice::SetSamples(std::vector<double> samples)
{
    std::lock_guard<std::mutex> lock(mutex_);
    samples_ = std::move(samples);
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

}  // 命名空间 VnaServiceTest
