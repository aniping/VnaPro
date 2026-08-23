#include "VnaHardwareSession.h"

#include <new>
#include <utility>

#include "VnaSessionController.h"

namespace vna::detail {

class VnaHardwareSession::Impl final {
public:
    Impl(DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter, VnaHardwareResourceLimits limits)
        : controller(std::move(descriptor), std::move(adapter), limits)
    {
    }

    // 对外薄包装只负责接口稳定性，全部并发和状态机语义集中在 controller。
    VnaSessionController controller;
};

Result<std::shared_ptr<VnaHardwareSession>> VnaHardwareSession::create(
    DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter, VnaHardwareResourceLimits limits)
{
    try {
        // controller 构造会启动线程，统一在工厂捕获异常并转换为领域错误。
        auto session = std::shared_ptr<VnaHardwareSession>(
            new VnaHardwareSession(std::move(descriptor), std::move(adapter), limits));
        return Result<std::shared_ptr<VnaHardwareSession>>::success(std::move(session));
    } catch (const std::bad_alloc &) {
        VnaError error;
        error.code = VnaErrorCode::ResourceExhausted;
        error.description = "hardware session allocation failed";
        return Result<std::shared_ptr<VnaHardwareSession>>::failure(std::move(error));
    } catch (const std::exception &exception) {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        error.description = exception.what();
        return Result<std::shared_ptr<VnaHardwareSession>>::failure(std::move(error));
    } catch (...) {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        error.description = "hardware session creation failed";
        return Result<std::shared_ptr<VnaHardwareSession>>::failure(std::move(error));
    }
}

VnaHardwareSession::VnaHardwareSession(
    DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter, VnaHardwareResourceLimits limits)
    : implementation_(std::make_unique<Impl>(std::move(descriptor), std::move(adapter), limits))
{
    // PImpl 隔离线程、插件适配器等私有类型，公开头文件不暴露实现依赖。
}

VnaHardwareSession::~VnaHardwareSession() = default;

DeviceDescriptor VnaHardwareSession::descriptor() const
{
    return implementation_->controller.descriptor();
}

VnaCapabilities VnaHardwareSession::capabilities() const
{
    return implementation_->controller.capabilities();
}

DeviceState VnaHardwareSession::state() const
{
    return implementation_->controller.state();
}

ValidationReport VnaHardwareSession::validate(const VnaSweepPlan &plan) const
{
    return implementation_->controller.validate(plan);
}

Result<CommandId> VnaHardwareSession::connectAsync()
{
    return implementation_->controller.connectAsync();
}

Result<CommandId> VnaHardwareSession::prepareAsync(VnaSweepPlan plan)
{
    // 计划按值进入异步边界，调用者返回后可立即修改或销毁原对象。
    return implementation_->controller.prepareAsync(std::move(plan));
}

Result<CommandId> VnaHardwareSession::startAsync(ConfigurationId configurationId)
{
    return implementation_->controller.startAsync(configurationId);
}

Result<CommandId> VnaHardwareSession::abortAsync(SweepId sweepId)
{
    return implementation_->controller.abortAsync(sweepId);
}

Result<CommandId> VnaHardwareSession::disconnectAsync()
{
    return implementation_->controller.disconnectAsync();
}

Subscription VnaHardwareSession::subscribe(IVnaHardwareEventSink &sink)
{
    // sink 由调用者持有；返回的 Subscription 负责保证退订后不再有在途回调。
    return implementation_->controller.subscribe(sink);
}

void VnaHardwareSession::shutdown()
{
    // 显式转发幂等关闭，便于 manager 在用户仍持有会话时先停止硬件活动。
    implementation_->controller.shutdown();
}

}  // namespace vna::detail
