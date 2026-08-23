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
    // 默认从驱动目录下的 vnapro.conf 读取日志和硬件资源配置。
    static Result<std::unique_ptr<VnaHardwareManager>> create(std::string driverDirectory);
    // 显式配置入口适用于配置文件不与驱动同目录的部署；两条路径都在创建阶段完成校验。
    static Result<std::unique_ptr<VnaHardwareManager>> create(
        std::string driverDirectory, std::string configurationFile);
    ~VnaHardwareManager();
    VnaHardwareManager(VnaHardwareManager &&other) noexcept;
    VnaHardwareManager &operator=(VnaHardwareManager &&other) noexcept;
    VnaHardwareManager(const VnaHardwareManager &) = delete;
    VnaHardwareManager &operator=(const VnaHardwareManager &) = delete;

    // 返回的 DiscoveryId 只用于关联本次异步回调，不是设备身份。
    DiscoveryId discoverAsync(const DiscoveryOptions &options, IVnaDiscoverySink &sink);
    // open 只接受本管理器发现并登记的 DeviceId。
    Result<std::shared_ptr<IVnaHardware>> open(const std::string &deviceId);
    // shutdown 需与 discover/open 及本管理器打开的全部会话 API 由上层同步。
    // 开始关闭后不得再使用既有会话；返回后不再产生发现或设备事件回调。
    void shutdown();

private:
    class Impl;
    explicit VnaHardwareManager(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna

#endif
