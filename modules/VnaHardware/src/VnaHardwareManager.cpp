#include "VnaHardwareManager.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "DriverModuleAdapter.h"
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

    void shutdownSessions(const std::vector<std::weak_ptr<detail::VnaHardwareSession>> &sessions)
    {
        for (const auto &weakSession : sessions) {
            if (auto session = weakSession.lock()) {
                session->shutdown();
            }
        }
    }

    class PendingDeviceHandle final {
    public:
        PendingDeviceHandle(std::shared_ptr<detail::DriverModule> module, VnaDeviceHandle handle)
            : module_(std::move(module)), handle_(handle)
        {
        }

        ~PendingDeviceHandle()
        {
            if (handle_ != nullptr) {
                module_->api().DestroyDevice(handle_);
            }
        }

        PendingDeviceHandle(const PendingDeviceHandle &) = delete;
        PendingDeviceHandle &operator=(const PendingDeviceHandle &) = delete;
        PendingDeviceHandle(PendingDeviceHandle &&) = delete;
        PendingDeviceHandle &operator=(PendingDeviceHandle &&) = delete;

        VnaDeviceHandle get() const noexcept
        {
            return handle_;
        }

        void release() noexcept
        {
            handle_ = nullptr;
        }

    private:
        std::shared_ptr<detail::DriverModule> module_;
        VnaDeviceHandle handle_{nullptr};
    };

}  // namespace

VnaHardwareManager::Impl::Impl(
    std::shared_ptr<detail::VnaLogger> logger, std::vector<std::shared_ptr<detail::DriverModule>> modules,
    detail::VnaHardwareResourceLimits limits)
    : logger_(std::move(logger)), limits_(limits), modules_(std::move(modules))
{
}

VnaHardwareManager::Impl::~Impl()
{
    shutdown();
}

Result<std::shared_ptr<IVnaHardware>> VnaHardwareManager::Impl::open(const std::string &deviceId)
{
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "正在打开设备，deviceId=" + deviceId);
    auto found = findDeviceForOpen(deviceId);
    if (!found.hasValue()) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(found.error());
    }
    DeviceRecord record = found.takeValue();
    auto handle = record.module->createDevice(record.driverDeviceId);
    if (!handle.hasValue()) {
        detail::VnaLogger::write(
            detail::VnaLogLevel::Error, "创建设备失败，deviceId=" + deviceId + "，原因=" + handle.error().description);
        return Result<std::shared_ptr<IVnaHardware>>::failure(handle.error());
    }
    PendingDeviceHandle pending(record.module, handle.takeValue());
    try {
        // adapter 同时持有设备句柄和模块引用；构造成功后才从临时 RAII 守卫移交所有权。
        auto adapter = std::make_unique<detail::DriverModuleAdapter>(record.module, pending.get());
        pending.release();
        auto session = detail::VnaHardwareSession::create(record.descriptor, std::move(adapter), limits_);
        if (!session.hasValue()) {
            detail::VnaLogger::write(
                detail::VnaLogLevel::Error,
                "创建会话失败，deviceId=" + deviceId + "，原因=" + session.error().description);
            return Result<std::shared_ptr<IVnaHardware>>::failure(session.error());
        }
        auto published = publishSession(session.takeValue());
        if (published.hasValue()) {
            detail::VnaLogger::write(detail::VnaLogLevel::Info, "设备已打开，deviceId=" + deviceId);
        }
        return published;
    } catch (const std::bad_alloc &) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(
            managerError(VnaErrorCode::ResourceExhausted, "hardware session allocation failed"));
    } catch (const std::exception &error) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(managerError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(
            managerError(VnaErrorCode::InternalError, "hardware session creation failed"));
    }
}

Result<VnaHardwareManager::Impl::DeviceRecord> VnaHardwareManager::Impl::findDeviceForOpen(const std::string &deviceId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_.load()) {
        return Result<DeviceRecord>::failure(
            managerError(VnaErrorCode::InvalidState, "hardware manager is shutting down"));
    }
    const auto device = devices_.find(deviceId);
    if (device == devices_.end()) {
        return Result<DeviceRecord>::failure(
            managerError(VnaErrorCode::DeviceNotFound, "device identifier is unknown"));
    }
    // 复制 module shared_ptr 后立即释放 manager 锁，插件调用绝不发生在全局锁内。
    return Result<DeviceRecord>::success(device->second);
}

Result<std::shared_ptr<IVnaHardware>> VnaHardwareManager::Impl::publishSession(
    std::shared_ptr<detail::VnaHardwareSession> session)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        // 创建驱动句柄期间 shutdown 可能已开始；发布前在同一锁下二次确认。
        if (stopping_.load()) {
            return Result<std::shared_ptr<IVnaHardware>>::failure(
                managerError(VnaErrorCode::InvalidState, "hardware manager is shutting down"));
        }
        // manager 仅保存 weak_ptr；先清理过期项，避免反复 open/release 使元数据无界增长。
        sessions_.erase(
            std::remove_if(sessions_.begin(), sessions_.end(), [](const auto &item) { return item.expired(); }),
            sessions_.end());
        sessions_.push_back(session);
        return Result<std::shared_ptr<IVnaHardware>>::success(std::move(session));
    } catch (const std::bad_alloc &) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(
            managerError(VnaErrorCode::ResourceExhausted, "hardware session registry allocation failed"));
    } catch (const std::exception &error) {
        return Result<std::shared_ptr<IVnaHardware>>::failure(managerError(VnaErrorCode::InternalError, error.what()));
    }
}

void VnaHardwareManager::Impl::shutdown()
{
    std::vector<std::thread> discoveryThreads;
    std::vector<std::weak_ptr<detail::VnaHardwareSession>> sessions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 与 publishSession 共用线性化锁，置位后不会再发布关闭流程未捕获的新会话。
        if (stopping_.load()) {
            return;
        }
        stopping_.store(true);
        takeShutdownResources(discoveryThreads, sessions);
    }
    detail::VnaLogger::write(detail::VnaLogLevel::Info, "硬件管理器开始关闭");
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
    std::vector<std::thread> &discoveryThreads, std::vector<std::weak_ptr<detail::VnaHardwareSession>> &sessions)
{
    // 调用者持有 mutex_；这里只转移所有权，耗时的 join/shutdown 在锁外执行。
    discoveryThreads = std::move(discoveryThreads_);
    sessions = std::move(sessions_);
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
