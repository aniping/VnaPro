/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_DEVICE_SESSION_H

#include <cstdint>
#include <string>
#include <vector>

#include "vna_core/sweep_plan.h"

namespace VnaCore {
namespace Detail {

// 仅由核心工作线程调用；失败时抛出异常，并说明失败的设备步骤。
class DeviceSession {
public:
    DeviceSession() = default;
    DeviceSession(const DeviceSession &) = delete;
    DeviceSession &operator=(const DeviceSession &) = delete;
    DeviceSession(DeviceSession &&) = delete;
    DeviceSession &operator=(DeviceSession &&) = delete;
    // 允许通过工作线程持有的多态设备接口析构对象。
    virtual ~DeviceSession() = default;
    // 初始化设备并返回设备标识；失败时报告失败的原生调用步骤。
    virtual std::string Initialize() = 0;
    // 在工作线程中应用独立持有的计划；准备阶段的失败必须发生在设备状态变更前。
    virtual void ApplyPlan(const SweepPlan &plan) = 0;
    // 运行开始前启用射频；仅由核心工作线程调用。
    virtual void EnableRf() = 0;
    // 每个频率返回物理端口 1～4 的完整 IQ 记录；按采样点排列，每条含 16 个值。
    virtual std::vector<double> AcquirePort(const SweepPlan &plan, std::uint32_t sourcePort) = 0;
    // 尝试关闭射频并释放设备资源，报告发生的原生调用失败。
    virtual void Shutdown() = 0;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
