#ifndef VNA_HARDWARE_VNA_HARDWARE_MANAGER_H
#define VNA_HARDWARE_VNA_HARDWARE_MANAGER_H

#include <chrono>
#include <memory>
#include <string>

#include "Result.h"
#include "VnaHardware.h"

namespace vna {

struct DiscoveryOptions {
    // 当前实现把该超时作为模块之间的截止时间，不能抢占阻塞中的第三方回调。
    std::chrono::milliseconds timeout{5000};
};

class IVnaDiscoverySink {
public:
    virtual ~IVnaDiscoverySink();
    // 同一次发现可产生多个 found，随后严格产生一个 completed 或 failed。
    virtual void onDeviceFound(DiscoveryId discoveryId, const DeviceDescriptor &descriptor) = 0;
    virtual void onDiscoveryCompleted(DiscoveryId discoveryId) = 0;
    virtual void onDiscoveryFailed(DiscoveryId discoveryId, const VnaError &error) = 0;
};

// 管理器拥有驱动模块；sink 必须存活到对应发现完成或失败回调。
class VnaHardwareManager final {
public:
    // 创建阶段完成目录信任校验、插件加载和 ABI 协商；无有效驱动时返回失败。
    static Result<std::unique_ptr<VnaHardwareManager>> create(std::string driverDirectory);
    ~VnaHardwareManager();
    VnaHardwareManager(VnaHardwareManager &&other) noexcept;
    VnaHardwareManager &operator=(VnaHardwareManager &&other) noexcept;
    VnaHardwareManager(const VnaHardwareManager &) = delete;
    VnaHardwareManager &operator=(const VnaHardwareManager &) = delete;

    // 返回的 DiscoveryId 只用于关联本次异步回调，不是设备身份。
    DiscoveryId discoverAsync(const DiscoveryOptions &options, IVnaDiscoverySink &sink);
    // open 只接受本管理器发现并登记的 DeviceId。
    Result<std::shared_ptr<IVnaHardware>> open(const std::string &deviceId);
    // shutdown 与新的 discover/open 调用需要由上层同步，返回后不再产生发现回调。
    void shutdown();

private:
    class Impl;
    explicit VnaHardwareManager(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna

#endif
