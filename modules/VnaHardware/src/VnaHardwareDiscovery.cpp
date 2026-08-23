#include "VnaHardwareManagerImpl.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "DriverModuleAdapter.h"
#include "VnaLogger.h"

namespace vna {
namespace {

    VnaError discoveryError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    std::string driverIdentifier(const detail::DriverModule &module)
    {
        // 元数据仍属于动态库，先限制长度再复制，避免在模块卸载后引用悬空内存。
        const VnaStringViewV1 view = module.api().metadata.driverId;
        if ((view.data == nullptr) || (view.size == 0) || (view.size > 256)) {
            return {};
        }
        return std::string(view.data, view.size);
    }

    std::string opaqueDeviceId(const std::string &driverId, const std::string &driverDeviceId)
    {
        // 稳定散列只用于生成不透明标识，不承担密码学认证用途。
        constexpr std::uint64_t offset = UINT64_C(14695981039346656037);
        constexpr std::uint64_t prime = UINT64_C(1099511628211);
        std::uint64_t hash = offset;
        const auto mix = [&hash](const std::string &value) {
            for (const unsigned char byte : value) {
                hash ^= byte;
                hash *= prime;
            }
        };
        mix(driverId);
        // 显式分隔两个命名段，避免 ("ab", "c") 与 ("a", "bc") 产生同一输入串。
        hash ^= UINT8_C(0xff);
        hash *= prime;
        mix(driverDeviceId);
        std::ostringstream stream;
        stream << std::hex << std::setw(16) << std::setfill('0') << hash;
        return stream.str();
    }

    void notifyDiscoveryFailure(IVnaDiscoverySink *sink, DiscoveryId identifier, const VnaError &error) noexcept
    {
        // 用户回调位于异常边界之外；回调异常不得穿透发现线程导致 std::terminate。
        try {
            sink->onDiscoveryFailed(identifier, error);
        } catch (...) {
        }
    }

    bool notifyDeviceFound(IVnaDiscoverySink *sink, DiscoveryId identifier, const DeviceDescriptor &descriptor) noexcept
    {
        try {
            sink->onDeviceFound(identifier, descriptor);
            return true;
        } catch (...) {
            return false;
        }
    }

    void notifyDiscoveryCompleted(IVnaDiscoverySink *sink, DiscoveryId identifier) noexcept
    {
        try {
            sink->onDiscoveryCompleted(identifier);
        } catch (...) {
        }
    }

}  // namespace

bool VnaHardwareManager::Impl::discoveryWasInterrupted(DiscoveryRunResult result)
{
    return (result == DiscoveryRunResult::Stopped) || (result == DiscoveryRunResult::CallbackFailed);
}

DiscoveryId VnaHardwareManager::Impl::discover(const DiscoveryOptions &options, IVnaDiscoverySink &sink)
{
    const DiscoveryId identifier = nextDiscovery_.fetch_add(1);
    detail::VnaLogger::write(
        detail::VnaLogLevel::Info, "设备发现已启动，discoveryId=" + std::to_string(identifier) +
                                       "，timeoutMs=" + std::to_string(options.timeout.count()));
    std::lock_guard<std::mutex> lock(mutex_);
    // 线程对象在 manager 内集中保存，析构时才能逐一 join，避免后台访问已销毁状态。
    if (!stopping_.load()) {
        discoveryThreads_.emplace_back(
            [this, identifier, options, sinkPointer = &sink] { runDiscovery(identifier, options, sinkPointer); });
    }
    return identifier;
}

bool VnaHardwareManager::Impl::registerDevice(
    const std::shared_ptr<detail::DriverModule> &module, std::pair<DeviceDescriptor, std::string> discovered,
    DeviceDescriptor &descriptor)
{
    const std::string moduleId = driverIdentifier(*module);
    if (moduleId.empty() || discovered.second.empty()) {
        return false;
    }
    // 对外 ID 不泄漏驱动内部地址，同时把驱动命名空间纳入哈希避免设备名碰撞。
    discovered.first.deviceId = opaqueDeviceId(moduleId, discovered.second);
    DeviceRecord record{discovered.first, std::move(discovered.second), module};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (deviceIdCollides(record)) {
            return false;
        }
        devices_[record.descriptor.deviceId] = record;
    }
    descriptor = std::move(discovered.first);
    detail::VnaLogger::write(
        detail::VnaLogLevel::Info, "发现设备，deviceId=" + descriptor.deviceId + "，model=" + descriptor.model +
                                       "，ports=" + std::to_string(descriptor.portCount));
    return true;
}

bool VnaHardwareManager::Impl::deviceIdCollides(const DeviceRecord &record) const
{
    const auto existing = devices_.find(record.descriptor.deviceId);
    if (existing == devices_.end()) {
        return false;
    }
    // 哈希相同只有在模块和原始设备 ID 都相同时才视为同一设备，否则拒绝覆盖旧记录。
    return (existing->second.driverDeviceId != record.driverDeviceId) || (existing->second.module != record.module);
}

void VnaHardwareManager::Impl::runDiscovery(DiscoveryId identifier, DiscoveryOptions options, IVnaDiscoverySink *sink)
{
    try {
        // 超时只在模块边界检查；单个驱动调用本身应由驱动遵守发现超时契约。
        const auto deadline = std::chrono::steady_clock::now() + options.timeout;
        bool moduleSucceeded = false;
        const DiscoveryRunResult runResult = scanModules(identifier, deadline, sink, moduleSucceeded);
        if (discoveryWasInterrupted(runResult)) {
            return;
        }
        if (runResult == DiscoveryRunResult::TimedOut) {
            notifyDiscoveryFailure(
                sink, identifier, discoveryError(VnaErrorCode::Timeout, "device discovery timed out"));
            return;
        }
        finishDiscovery(identifier, sink, moduleSucceeded);
    } catch (...) {
        if (!stopping_.load()) {
            notifyDiscoveryFailure(
                sink, identifier, discoveryError(VnaErrorCode::InternalError, "device discovery task failed"));
        }
    }
}

void VnaHardwareManager::Impl::finishDiscovery(DiscoveryId identifier, IVnaDiscoverySink *sink, bool moduleSucceeded)
{
    if (stopping_.load()) {
        return;
    }
    if (moduleSucceeded) {
        detail::VnaLogger::write(detail::VnaLogLevel::Info, "设备发现完成，discoveryId=" + std::to_string(identifier));
        notifyDiscoveryCompleted(sink, identifier);
        return;
    }
    detail::VnaLogger::write(detail::VnaLogLevel::Error, "设备发现失败，discoveryId=" + std::to_string(identifier));
    notifyDiscoveryFailure(
        sink, identifier, discoveryError(VnaErrorCode::DriverLoadFailed, "all drivers failed discovery"));
}

VnaHardwareManager::Impl::DiscoveryRunResult VnaHardwareManager::Impl::scanModules(
    DiscoveryId identifier, std::chrono::steady_clock::time_point deadline, IVnaDiscoverySink *sink,
    bool &moduleSucceeded)
{
    for (const auto &module : modules_) {
        if (stopping_.load()) {
            return DiscoveryRunResult::Stopped;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            return DiscoveryRunResult::TimedOut;
        }
        if (!discoverModule(module, identifier, sink, moduleSucceeded)) {
            return DiscoveryRunResult::CallbackFailed;
        }
    }
    return DiscoveryRunResult::Completed;
}

bool VnaHardwareManager::Impl::discoverModule(
    const std::shared_ptr<detail::DriverModule> &module, DiscoveryId identifier, IVnaDiscoverySink *sink,
    bool &moduleSucceeded)
{
    auto result = module->discover();
    if (!result.hasValue()) {
        return true;
    }
    // 驱动成功返回空列表也算一次有效发现，最终应报告 completed 而不是“全部失败”。
    moduleSucceeded = true;
    return publishDevices(module, result.value(), identifier, sink);
}

bool VnaHardwareManager::Impl::publishDevices(
    const std::shared_ptr<detail::DriverModule> &module, std::vector<std::pair<DeviceDescriptor, std::string>> &devices,
    DiscoveryId identifier, IVnaDiscoverySink *sink)
{
    for (auto &found : devices) {
        if (stopping_.load()) {
            return false;
        }
        DeviceDescriptor descriptor;
        // 先写入宿主设备表，再通知观察者，回调中立即 open 时记录已经可见。
        if (!registerDevice(module, std::move(found), descriptor)) {
            continue;
        }
        if (notifyDeviceFound(sink, identifier, descriptor)) {
            continue;
        }
        notifyDiscoveryFailure(sink, identifier, discoveryError(VnaErrorCode::InternalError, "device callback failed"));
        return false;
    }
    return true;
}

}  // namespace vna
