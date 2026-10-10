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
    // 阻塞关卡状态和工作线程标识始终绑定到本实例。
    ControlledDevice() = default;
    ControlledDevice(const ControlledDevice &) = delete;
    ControlledDevice &operator=(const ControlledDevice &) = delete;
    ControlledDevice(ControlledDevice &&) = delete;
    ControlledDevice &operator=(ControlledDevice &&) = delete;
    ~ControlledDevice() override = default;
    // 返回固定的受控设备标识，不调用原生驱动。
    std::string Initialize() override;

    // 应用冻结的扫描计划，或在测试控制的关卡处阻塞、失败。
    void ApplyPlan(const VnaCore::SweepPlan &) override;

    // 受控扫描无需执行原生射频操作，进入准备状态时不会失败。
    void EnableRf() noexcept override;

    // 本设备不持有原生射频资源或加载器句柄，关闭时无需清理这些资源。
    void Shutdown() noexcept override;

    // 已启用的阻塞关卡释放后，返回完整且确定的 IQ 数据向量。
    std::vector<double> AcquirePort(const VnaCore::SweepPlan &plan, std::uint32_t) override;

    // 持有设备互斥锁时，切换用于测试的 NaN 或无穷值注入开关。
    void SetInvalidSamples(bool value);

    // 在设备状态改变前拒绝下一次计划准备，使初始化尝试可以重试。
    void FailNextPlanPreparation();

    // 持有设备互斥锁时，读取是否已进入采集阻塞关卡。
    bool HasEntered() const;

    // 持有设备互斥锁时，读取是否已进入配置阻塞关卡。
    bool HasPlanEntered() const;

    // 复位并启用下一次配置阻塞关卡。
    void ArmPlan();

    // 复位并启用下一次采集阻塞关卡；按配置在关卡释放后模拟失败。
    void Arm(bool fail = false);

    // 释放配置和采集阻塞关卡，避免测试夹具清理时挂起。
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
}  // 命名空间 VnaServiceTest
#endif
