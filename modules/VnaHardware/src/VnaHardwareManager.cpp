#include "VnaHardwareManager.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "DriverPluginLoader.h"
#include "VnaHardwareManagerImpl.h"
#include "VnaHardwareSession.h"
#include "VnaLogger.h"

namespace vna {
namespace {

    VnaError managerError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    void joinThreads(std::vector<std::thread> &threads)
    {
        // 线程均由 manager 创建并拥有，不 detach，确保析构后没有后台访问 sink 或模块。
        for (auto &thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void shutdownSessions(const std::vector<std::shared_ptr<detail::VnaHardwareSession>> &sessions)
    {
        for (const auto &session : sessions) {
            session->shutdown();
        }
    }

    std::string defaultConfigurationFile(const std::string &driverDirectory)
    {
        if (driverDirectory.empty()) {
            return "vnapro.conf";
        }
        const bool hasSeparator = !driverDirectory.empty() && (driverDirectory.back() == '/');
        return driverDirectory + (hasSeparator ? "vnapro.conf" : "/vnapro.conf");
    }

}  // namespace

VnaHardwareManager::Impl::Impl(
    std::shared_ptr<detail::VnaLogger> logger, std::vector<std::shared_ptr<detail::DriverModule>> modules)
    : logger_(std::move(logger)), modules_(std::move(modules))
{
}

VnaHardwareManager::Impl::~Impl()
{
    shutdown();
}

Result<std::shared_ptr<IVnaHardware>> VnaHardwareManager::Impl::open(const std::string &deviceId)
{
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "正在打开设备，deviceId=" + deviceId);
    DeviceRecord record;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_.load()) {
            return Result<std::shared_ptr<IVnaHardware>>::failure(
                managerError(VnaErrorCode::InvalidState, "hardware manager is shutting down"));
        }
        const auto device = devices_.find(deviceId);
        if (device == devices_.end()) {
            return Result<std::shared_ptr<IVnaHardware>>::failure(
                managerError(VnaErrorCode::DeviceNotFound, "device identifier is unknown"));
        }
        // 复制记录中的 module shared_ptr 后立即释放 manager 锁，插件调用绝不发生在全局锁内。
        record = device->second;
    }
    auto handle = record.module->createDevice(record.driverDeviceId);
    if (!handle.hasValue()) {
        detail::VnaLogger::write(
            detail::VnaLogLevel::Error, "创建设备失败，deviceId=" + deviceId + "，原因=" + handle.error().description);
        return Result<std::shared_ptr<IVnaHardware>>::failure(handle.error());
    }
    // adapter 同时持有设备句柄和模块引用，保证设备整个生命周期内动态库不会卸载。
    auto adapter = std::make_unique<detail::DriverModuleAdapter>(record.module, handle.takeValue());
    auto session = detail::VnaHardwareSession::create(record.descriptor, std::move(adapter));
    if (!session.hasValue()) {
        detail::VnaLogger::write(
            detail::VnaLogLevel::Error, "创建会话失败，deviceId=" + deviceId + "，原因=" + session.error().description);
        return Result<std::shared_ptr<IVnaHardware>>::failure(session.error());
    }
    std::shared_ptr<detail::VnaHardwareSession> concrete = session.takeValue();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // manager 仅保存 weak_ptr，用户释放会话时不会被管理器意外延长设备占用时间。
        sessions_.push_back(concrete);
    }
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "设备已打开，deviceId=" + deviceId);
    return Result<std::shared_ptr<IVnaHardware>>::success(std::move(concrete));
}

void VnaHardwareManager::Impl::shutdown()
{
    // exchange 使显式 shutdown 与析构并发调用保持幂等。
    if (stopping_.exchange(true)) {
        return;
    }
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "硬件管理器开始关闭");
    std::vector<std::thread> discoveryThreads;
    std::vector<std::shared_ptr<detail::VnaHardwareSession>> sessions;
    takeShutdownResources(discoveryThreads, sessions);
    // 先等待发现线程停止，再关闭会话，避免回调与设备析构交错。
    joinThreads(discoveryThreads);
    shutdownSessions(sessions);
    // 会话已经停止，最后清空模块引用，动态库卸载不会再与回调交错。
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    devices_.clear();
    modules_.clear();
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "硬件管理器关闭完成");
}

void VnaHardwareManager::Impl::takeShutdownResources(
    std::vector<std::thread> &discoveryThreads, std::vector<std::shared_ptr<detail::VnaHardwareSession>> &sessions)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 锁内只转移所有权；耗时的 join/shutdown 在锁外执行，避免关闭路径互相等待。
    discoveryThreads = std::move(discoveryThreads_);
    for (const auto &weakSession : sessions_) {
        if (auto session = weakSession.lock()) {
            sessions.push_back(std::move(session));
        }
    }
}

Result<std::unique_ptr<VnaHardwareManager>> VnaHardwareManager::create(std::string driverDirectory)
{
    return create(driverDirectory, defaultConfigurationFile(driverDirectory));
}

Result<std::unique_ptr<VnaHardwareManager>> VnaHardwareManager::create(
    std::string driverDirectory, std::string configurationFile)
{
    auto logger = detail::VnaLogger::acquire(configurationFile);
    if (!logger.hasValue()) {
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(logger.error());
    }
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "正在创建硬件管理器，驱动目录=" + driverDirectory);
    // 模块目录整体装载成功后才发布 manager，调用者不会拿到部分初始化对象。
    auto modules = detail::DriverPluginLoader::loadDirectory(driverDirectory);
    if (!modules.hasValue()) {
        detail::VnaLogger::write(detail::VnaLogLevel::Error, "驱动模块加载失败：" + modules.error().description);
        return Result<std::unique_ptr<VnaHardwareManager>>::failure(modules.error());
    }
    auto implementation = std::make_unique<Impl>(logger.takeValue(), modules.takeValue());
    auto manager = std::unique_ptr<VnaHardwareManager>(new VnaHardwareManager(std::move(implementation)));
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "硬件管理器创建完成");
    return Result<std::unique_ptr<VnaHardwareManager>>::success(std::move(manager));
}

VnaHardwareManager::VnaHardwareManager(std::unique_ptr<Impl> implementation)
    : implementation_(std::move(implementation))
{
}

VnaHardwareManager::~VnaHardwareManager() = default;
VnaHardwareManager::VnaHardwareManager(VnaHardwareManager &&other) noexcept = default;
VnaHardwareManager &VnaHardwareManager::operator=(VnaHardwareManager &&other) noexcept = default;

DiscoveryId VnaHardwareManager::discoverAsync(const DiscoveryOptions &options, IVnaDiscoverySink &sink)
{
    return implementation_->discover(options, sink);
}

Result<std::shared_ptr<IVnaHardware>> VnaHardwareManager::open(const std::string &deviceId)
{
    return implementation_->open(deviceId);
}

void VnaHardwareManager::shutdown()
{
    implementation_->shutdown();
}

}  // namespace vna
