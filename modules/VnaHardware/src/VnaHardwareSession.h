#ifndef VNA_HARDWARE_VNA_HARDWARE_SESSION_H
#define VNA_HARDWARE_VNA_HARDWARE_SESSION_H

#include <memory>

#include "DriverModuleAdapter.h"
#include "VnaHardware.h"

namespace vna::detail {

// 公共接口的薄封装，实际线程和状态生命周期集中在 VnaSessionController。
class VnaHardwareSession final : public IVnaHardware {
public:
    // 工厂把线程创建失败转换成 Result，避免构造异常穿过公开硬件接口。
    static Result<std::shared_ptr<VnaHardwareSession>> create(
        DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter);
    ~VnaHardwareSession() override;
    VnaHardwareSession(const VnaHardwareSession &) = delete;
    VnaHardwareSession &operator=(const VnaHardwareSession &) = delete;

    DeviceDescriptor descriptor() const override;
    VnaCapabilities capabilities() const override;
    DeviceState state() const override;
    ValidationReport validate(const VnaSweepPlan &plan) const override;
    // 异步命令只返回关联 ID，成功/失败统一通过订阅事件到达调用者。
    CommandId connectAsync() override;
    CommandId prepareAsync(VnaSweepPlan plan) override;
    CommandId startAsync(ConfigurationId configurationId) override;
    CommandId abortAsync(SweepId sweepId) override;
    CommandId disconnectAsync() override;
    Subscription subscribe(IVnaHardwareEventSink &sink) override;
    void shutdown();

private:
    // implementation_ 唯一拥有 controller，成员析构会触发完整有序停机。
    class Impl;
    VnaHardwareSession(DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna::detail

#endif
