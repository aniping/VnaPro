#ifndef VNA_HARDWARE_VNA_HARDWARE_MANAGER_IMPL_H
#define VNA_HARDWARE_VNA_HARDWARE_MANAGER_IMPL_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "VnaHardwareManager.h"

namespace vna {
namespace detail {

    class DriverModule;
    class VnaHardwareSession;

}  // namespace detail

class VnaHardwareManager::Impl final {
public:
    struct DeviceRecord {
        // descriptor 使用宿主不透明 ID；driverDeviceId 仅在回调插件 CreateDevice 时使用。
        DeviceDescriptor descriptor;
        std::string driverDeviceId;
        std::shared_ptr<detail::DriverModule> module;
    };

    explicit Impl(std::vector<std::shared_ptr<detail::DriverModule>> modules);
    ~Impl();
    DiscoveryId discover(const DiscoveryOptions &options, IVnaDiscoverySink &sink);
    Result<std::shared_ptr<IVnaHardware>> open(const std::string &deviceId);
    void shutdown();

private:
    // 区分正常完成、停止、超时和回调失败，避免发现循环混合终态通知。
    enum class DiscoveryRunResult : std::uint8_t { Completed, Stopped, TimedOut, CallbackFailed };

    static bool discoveryWasInterrupted(DiscoveryRunResult result);
    void runDiscovery(DiscoveryId identifier, DiscoveryOptions options, IVnaDiscoverySink *sink);
    DiscoveryRunResult scanModules(
        DiscoveryId identifier, std::chrono::steady_clock::time_point deadline, IVnaDiscoverySink *sink,
        bool &moduleSucceeded);
    bool discoverModule(
        const std::shared_ptr<detail::DriverModule> &module, DiscoveryId identifier, IVnaDiscoverySink *sink,
        bool &moduleSucceeded);
    void finishDiscovery(DiscoveryId identifier, IVnaDiscoverySink *sink, bool moduleSucceeded);
    bool publishDevices(
        const std::shared_ptr<detail::DriverModule> &module,
        std::vector<std::pair<DeviceDescriptor, std::string>> &devices, DiscoveryId identifier,
        IVnaDiscoverySink *sink);
    bool registerDevice(
        const std::shared_ptr<detail::DriverModule> &module, std::pair<DeviceDescriptor, std::string> discovered,
        DeviceDescriptor &descriptor);
    // 调用者持有 mutex_，避免检查与插入之间出现竞态。
    bool deviceIdCollides(const DeviceRecord &record) const;
    void takeShutdownResources(
        std::vector<std::thread> &discoveryThreads, std::vector<std::shared_ptr<detail::VnaHardwareSession>> &sessions);

    // modules_ 必须最后释放，发现记录、会话适配器和函数表都依赖动态库仍被装载。
    std::vector<std::shared_ptr<detail::DriverModule>> modules_;
    std::mutex mutex_;
    std::map<std::string, DeviceRecord> devices_;
    // 发现线程由 manager 唯一拥有并 join；sessions_ 用弱引用避免延长用户会话寿命。
    std::vector<std::thread> discoveryThreads_;
    std::vector<std::weak_ptr<detail::VnaHardwareSession>> sessions_;
    // stopping_ 是线程间快速终止标志；容器读写仍必须持有 mutex_。
    std::atomic<bool> stopping_{false};
    std::atomic<DiscoveryId> nextDiscovery_{1};
};

}  // namespace vna

#endif
