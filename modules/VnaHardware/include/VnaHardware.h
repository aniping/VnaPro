#ifndef VNA_HARDWARE_VNA_HARDWARE_H
#define VNA_HARDWARE_VNA_HARDWARE_H

#include <string>

#include "VnaCapabilities.h"
#include "VnaHardwareEvent.h"

namespace vna {

struct DeviceDescriptor {
    // deviceId 是管理器生成的稳定不透明标识，不等同于驱动内部设备名。
    std::string deviceId;
    std::string vendor;
    std::string model;
    std::string serialNumber;
    std::uint16_t portCount{0};
};

// 设备命令异步返回 CommandId；最终结果只通过订阅事件交付，调用方不得据返回值推断成功。
// 同一对象的查询、订阅和异步命令可由多线程并发调用，调用方无需持锁，会话内部负责同步。
// 例外是管理器 shutdown 生命周期边界：关闭期间不得再调用本对象的任何方法。
class IVnaHardware {
public:
    virtual ~IVnaHardware();
    // 查询返回内部快照的副本，不触发设备 I/O。
    virtual DeviceDescriptor descriptor() const = 0;
    virtual VnaCapabilities capabilities() const = 0;
    virtual DeviceState state() const = 0;
    // validate 只做快速本地校验；厂商组合约束由 prepareAsync 最终确认。
    virtual ValidationReport validate(const VnaSweepPlan &plan) const = 0;
    // 异步命令只保证成功入队，完成或失败必须按 CommandId 匹配事件。
    virtual CommandId connectAsync() = 0;
    virtual CommandId prepareAsync(VnaSweepPlan plan) = 0;
    virtual CommandId startAsync(ConfigurationId configurationId) = 0;
    virtual CommandId abortAsync(SweepId sweepId) = 0;
    virtual CommandId disconnectAsync() = 0;
    // 当前原型只允许一个订阅者；sink 必须比返回的 Subscription 活得更久。
    virtual Subscription subscribe(IVnaHardwareEventSink &sink) = 0;
};

}  // namespace vna

#endif
