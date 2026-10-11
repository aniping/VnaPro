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
    // 屏障状态和工作线程标识始终绑定到此实例。
    ControlledDevice() = default;
    ControlledDevice(const ControlledDevice &) = delete;
    ControlledDevice &operator=(const ControlledDevice &) = delete;
    ControlledDevice(ControlledDevice &&) = delete;
    ControlledDevice &operator=(ControlledDevice &&) = delete;
    ~ControlledDevice() override = default;
    // 返回受控设备的固定标识，不调用原生驱动。
    std::string Initialize() override;

    // 应用已冻结的计划，或在测试控制的屏障处阻塞或失败。
    void ApplyPlan(const VnaCore::SweepPlan &plan) override;

    // 开始受控运行，不启用原生射频输出。
    void EnableRf() override;

    // 等待已设置的屏障释放后，返回完整且可复现的 IQ 向量。
    std::vector<double> AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t source) override;

    // 记录或完成受控清理操作；注入的失败仍可被观察。
    void Shutdown() override;

    // 重置并设置下一次采集屏障；若启用故障注入，则在屏障释放后失败。
    void Arm();

    // 最多等待三秒，直到工作线程到达已设置的屏障。
    void WaitEntered();

    // 释放当前采集屏障，并唤醒等待中的工作线程。
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
    std::size_t initializeCalls_ = 0U;
    std::size_t applyPlanCalls_ = 0U;
    std::size_t enableRfCalls_ = 0U;
    std::size_t acquisitions_ = 0U;
    std::uint64_t acquisitionFrame_ = 0U;
    std::thread::id deviceThread_;
    bool wrongThread_ = false;
    bool shutdownCalled_ = false;
    VnaCore::SweepPlan applied_;

private:
    void RecordThread();
};

}  // 命名空间 VnaCoreTest

#endif
