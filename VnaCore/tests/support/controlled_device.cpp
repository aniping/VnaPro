/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "controlled_device.h"

namespace VnaCoreTest {

std::string ControlledDevice::Initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    RecordThread();
    ++initializeCalls_;
    return "TestDevice";
}

void ControlledDevice::ApplyPlan(const VnaCore::SweepPlan &plan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RecordThread();
    ++applyPlanCalls_;
    if (failPrepare_) {
        failPrepare_ = false;
        throw VnaCore::Detail::PlanPreparationError();
    }
    applied_ = plan;  // 先模拟部分状态变更，再报告失败。
    if (failApply_) {
        failApply_ = false;
        throw std::runtime_error("controlled partial configuration failure");
    }
}

void ControlledDevice::EnableRf()
{
    std::lock_guard<std::mutex> lock(mutex_);
    RecordThread();
    ++enableRfCalls_;
    acquisitionFrame_ = 0U;
}

std::vector<double> ControlledDevice::AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t source)
{
    std::unique_lock<std::mutex> lock(mutex_);
    RecordThread();
    Require(SamePlan(plan, applied_), "device received a plan different from its applied_ configuration");
    if (source == plan.sourcePorts.front()) {
        ++acquisitionFrame_;
    }
    ++acquisitions_;
    cv_.notify_all();
    if (blockNext_) {
        blockNext_ = false;
        entered_ = true;
        cv_.notify_all();
        cv_.wait(lock, [this] {
            return release_;
        });
    }
    if (acquisitions_ == failAcquireAt_) {
        throw std::runtime_error("controlled second-port acquisition failure");
    }
    std::vector<double> iq;
    iq.reserve(16U * plan.frequencyRaw.size());
    for (std::size_t point = 0U; point < plan.frequencyRaw.size(); ++point) {
        for (std::size_t receiver = 0U; receiver < 4U; ++receiver) {
            for (std::size_t component = 0U; component < 4U; ++component) {
                iq.push_back(SampleValue(plan, acquisitionFrame_, source, point, (receiver * 4U) + component));
            }
        }
    }
    if (acquisitions_ == shortAcquireAt_) {
        iq.pop_back();
    }
    return iq;
}

void ControlledDevice::Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    RecordThread();
    shutdownCalled_ = true;
    if (failShutdown_) {
        throw std::runtime_error("controlled RF-off failure");
    }
}

void ControlledDevice::Arm()
{
    std::lock_guard<std::mutex> lock(mutex_);
    blockNext_ = true;
    entered_ = false;
    release_ = false;
}

void ControlledDevice::WaitEntered()
{
    std::unique_lock<std::mutex> lock(mutex_);
    Require(cv_.wait_for(lock, std::chrono::seconds(3),
                [this] {
                    return entered_;
                }),
        "gate was never entered_");
}

void ControlledDevice::ReleaseGate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    release_ = true;
    cv_.notify_all();
}

void ControlledDevice::RecordThread()
{
    if (deviceThread_ == std::thread::id()) {
        deviceThread_ = std::this_thread::get_id();
    }
    wrongThread_ = wrongThread_ || deviceThread_ != std::this_thread::get_id();
}

}  // 命名空间 VnaCoreTest
