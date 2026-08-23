#ifndef VNA_HARDWARE_VNA_HARDWARE_H
#define VNA_HARDWARE_VNA_HARDWARE_H

#include <string>

#include "Result.h"
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

// 设备命令先同步报告准入结果；已准入命令的最终结果只通过订阅事件交付。
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
    // Result 成功表示命令已进入有界队列，随后恰有一个终态；同步失败不产生 CommandId 或回调。
    virtual Result<CommandId> connectAsync() = 0;
    virtual Result<CommandId> prepareAsync(VnaSweepPlan plan) = 0;
    virtual Result<CommandId> startAsync(ConfigurationId configurationId) = 0;
    virtual Result<CommandId> abortAsync(SweepId sweepId) = 0;
    virtual Result<CommandId> disconnectAsync() = 0;
    // 当前原型只允许一个订阅者；sink 必须比返回的 Subscription 活得更久。
    virtual Subscription subscribe(IVnaHardwareEventSink &sink) = 0;
};

}  // namespace vna

#endif
