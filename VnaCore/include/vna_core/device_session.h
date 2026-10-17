/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_DEVICE_SESSION_H
#define VNAPRO_VNA_CORE_DEVICE_SESSION_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "vna_core/port_frame.h"
#include "vna_core/sweep_plan.h"

namespace VnaCore {

// 两版驱动包含同名但签名不同的函数，必须在加载前明确选择接口版本。
enum class DriverApi {
    TDC1VN08,
    TDC1VN10
};

namespace Detail {

// 只在全部工作源均读齐此前缀后同步调用，引用只在回调期间有效。
using SweepProgressCallback = std::function<void(std::size_t, const std::vector<PortFrame> &)>;
// 整段 Get 前的主机等待；参数为完成后前缀长度，false 表示取消节拍并改用快速完整取回。
using SweepReadGate = std::function<bool(std::size_t)>;

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
    // 完成一次完整扫描，按计划的源端口顺序返回独立 IQ 缓冲区；每个频点含 16 个值。
    // 旧板卡内部逐源扫描，新板卡只启动一次整帧扫描。
    virtual std::vector<PortFrame> AcquireSweep(const SweepPlan &plan) = 0;

    // 支持分段取数的设备可报告真实完整前缀；旧接口默认仍只返回最终完整帧。
    virtual std::vector<PortFrame> AcquireSweep(const SweepPlan &plan, const SweepProgressCallback &progress);
    virtual std::vector<PortFrame> AcquireSweep(
        const SweepPlan &plan, const SweepProgressCallback &progress, const SweepReadGate &beforeRead);
    // 是否支持可打断的主机回读节拍；不声明硬件内部驻留或 ADC 定时能力。
    virtual bool ManualSweepTimeSupported() const noexcept;

    // 尝试关闭射频并释放设备资源，报告发生的原生调用失败。
    virtual void Shutdown() = 0;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
