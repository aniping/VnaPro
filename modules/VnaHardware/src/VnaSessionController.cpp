#include "VnaSessionController.h"

#include <exception>
#include <string>
#include <utility>

#include "HostServices.h"
#include "VnaBufferPool.h"
#include "VnaLogger.h"
#include "VnaValidation.h"

namespace vna::detail {
namespace {

    VnaError internalError(const char *description)
    {
        VnaError error;
        error.code = VnaErrorCode::InternalError;
        error.description = description;
        return error;
    }

    const char *stateName(DeviceState state)
    {
        static constexpr const char *names[]{"Disconnected", "Connecting", "Idle",     "Preparing",     "Prepared",
                                             "Starting",     "Running",    "Aborting", "Disconnecting", "Fault"};
        const std::size_t index = static_cast<std::size_t>(state);
        return index < (sizeof(names) / sizeof(names[0])) ? names[index] : "Unknown";
    }

}  // namespace

VnaSessionController::VnaSessionController(DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter)
    : descriptor_(std::move(descriptor)), adapter_(std::move(adapter)), dispatcher_(VnaEventDispatcher::create(64))
{
    try {
        // 采集线程只执行可能长时间阻塞的驱动调用，actor 线程独占会话状态机。
        workerThread_ = std::thread([this] { workerLoop(); });
        actorThread_ = std::thread([this] { actorLoop(); });
    } catch (...) {
        // 第二个线程创建失败时，必须回滚已经启动的线程和分发器，不能遗留半初始化对象。
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workerStopping_ = true;
        }
        workerReady_.notify_all();
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
        dispatcher_->shutdown();
        throw;
    }
}

VnaSessionController::~VnaSessionController()
{
    shutdown();
}

DeviceDescriptor VnaSessionController::descriptor() const
{
    return descriptor_;
}

VnaCapabilities VnaSessionController::capabilities() const
{
    // 返回值快照隔离内部状态，调用者不会观察到 prepare/断开过程中的中间修改。
    std::lock_guard<std::mutex> lock(dataMutex_);
    return capabilities_;
}

DeviceState VnaSessionController::state() const
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    return state_;
}

ValidationReport VnaSessionController::validate(const VnaSweepPlan &plan) const
{
    // 校验使用同一时刻的能力快照，避免连接状态切换时读到撕裂数据。
    std::lock_guard<std::mutex> lock(dataMutex_);
    return validatePlan(plan, capabilities_);
}

CommandId VnaSessionController::nextCommandId()
{
    return nextCommand_.fetch_add(1);
}

CommandId VnaSessionController::connectAsync()
{
    Command command;
    command.kind = CommandKind::Connect;
    command.commandId = nextCommandId();
    enqueue(command);
    return command.commandId;
}

CommandId VnaSessionController::prepareAsync(VnaSweepPlan plan)
{
    Command command;
    command.kind = CommandKind::Prepare;
    command.commandId = nextCommandId();
    command.plan = std::move(plan);
    const CommandId identifier = command.commandId;
    enqueue(std::move(command));
    return identifier;
}

CommandId VnaSessionController::startAsync(ConfigurationId configurationId)
{
    Command command;
    command.kind = CommandKind::Start;
    command.commandId = nextCommandId();
    command.configurationId = configurationId;
    enqueue(command);
    return command.commandId;
}

CommandId VnaSessionController::abortAsync(SweepId sweepId)
{
    Command command;
    command.kind = CommandKind::Abort;
    command.commandId = nextCommandId();
    command.sweepId = sweepId;
    enqueue(command);
    return command.commandId;
}

CommandId VnaSessionController::disconnectAsync()
{
    Command command;
    command.kind = CommandKind::Disconnect;
    command.commandId = nextCommandId();
    enqueue(command);
    return command.commandId;
}

Subscription VnaSessionController::subscribe(IVnaHardwareEventSink &sink)
{
    return dispatcher_->subscribe(sink);
}

void VnaSessionController::enqueue(Command command)
{
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        // 关闭后的异步请求直接丢弃；shutdown 已负责终止既有命令并封闭事件通道。
        if (actorStopping_) {
            return;
        }
        commands_.push_back(std::move(command));
    }
    commandReady_.notify_one();
}

void VnaSessionController::actorLoop()
{
    for (;;) {
        Command command;
        {
            std::unique_lock<std::mutex> lock(commandMutex_);
            commandReady_.wait(lock, [this] { return actorStopping_ || !commands_.empty(); });
            if (actorStopping_) {
                return;
            }
            // 只在取队列节点时持锁，具体命令可调用驱动或发布事件，不能占用队列锁。
            command = std::move(commands_.front());
            commands_.pop_front();
        }
        try {
            // 所有状态迁移在单一 actor 上串行执行，因此命令处理函数不需要另建状态机锁。
            process(std::move(command));
        } catch (...) {
            transition(DeviceState::Fault);
            fail(command.commandId, internalError("unhandled control task failure"));
        }
    }
}

void VnaSessionController::workerLoop()
{
    for (;;) {
        SweepId sweepId = 0;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerReady_.wait(lock, [this] { return workerStopping_ || workerHasTask_; });
            if (workerStopping_ && !workerHasTask_) {
                return;
            }
            sweepId = workerSweep_;
            workerHasTask_ = false;
        }
        // execute 可能覆盖完整扫描时长，独立线程保证 abort/disconnect 命令仍可被 actor 处理。
        VnaError result;
        try {
            result = adapter_->execute(sweepId);
        } catch (...) {
            result = internalError("unhandled acquisition task failure");
        }
        // 先撤销宿主缓冲区路由，再通知 actor 完成，迟到的驱动提交会被安全拒绝。
        HostServices::unregisterSweep(sweepId);
        Command completion;
        completion.kind = CommandKind::WorkerFinished;
        completion.sweepId = sweepId;
        completion.workerError = std::move(result);
        enqueue(std::move(completion));
    }
}

void VnaSessionController::submitSweep(SweepId sweepId)
{
    // actor 保证同一会话最多一个活动扫描，因此单槽 worker 邮箱足够且不会覆盖任务。
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerSweep_ = sweepId;
        workerHasTask_ = true;
    }
    workerReady_.notify_one();
}

void VnaSessionController::transition(DeviceState next)
{
    DeviceState previous;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        previous = state_;
        if (previous == next) {
            return;
        }
        state_ = next;
    }
    VnaLogger::write(
        VnaLogLevel::Info,
        "设备状态迁移，deviceId=" + descriptor_.deviceId + "，" + stateName(previous) + " -> " + stateName(next));
    dispatcher_->postStateChanged(previous, next);
}

void VnaSessionController::fail(CommandId commandId, VnaError error)
{
    error.deviceId = descriptor_.deviceId;
    error.commandId = commandId;
    VnaLogger::write(
        VnaLogLevel::Error,
        "异步命令失败，deviceId=" + descriptor_.deviceId + "，commandId=" + std::to_string(commandId) +
            "，code=" + std::to_string(static_cast<std::uint32_t>(error.code)) + "，原因=" + error.description);
    dispatcher_->postCommandFailed(commandId, std::move(error));
}

}  // namespace vna::detail
