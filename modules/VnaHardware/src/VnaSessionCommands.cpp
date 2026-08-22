#include "VnaSessionController.h"

#include <string>
#include <utility>

#include "VnaValidation.h"

namespace vna::detail {
namespace {

    // 状态机内部先构造无上下文错误，统一由 fail() 补齐设备与命令标识。
    VnaError commandError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

}  // namespace

void VnaSessionController::process(Command command)
{
    // 每种命令都显式列出，新增状态机输入时由编译器和规范检查共同暴露遗漏。
    switch (command.kind) {
        case CommandKind::Connect:
            processConnect(command);
            break;
        case CommandKind::Prepare:
            processPrepare(std::move(command));
            break;
        case CommandKind::Start:
            processStart(command);
            break;
        case CommandKind::Abort:
            processAbort(command);
            break;
        case CommandKind::Disconnect:
            processDisconnect(command);
            break;
        case CommandKind::WorkerFinished:
            processWorkerFinished(command);
            break;
    }
}

void VnaSessionController::processConnect(const Command &command)
{
    // 连接只允许从稳定的 Disconnected 状态进入，重复连接必须显式失败。
    if (state() != DeviceState::Disconnected) {
        fail(command.commandId, commandError(VnaErrorCode::InvalidState, "connect requires Disconnected"));
        return;
    }
    transition(DeviceState::Connecting);
    auto connected = adapter_->connect();
    if (!connected.hasValue()) {
        // 连接失败没有产生可用设备能力，因此回退到可重试的 Disconnected。
        transition(DeviceState::Disconnected);
        fail(command.commandId, connected.error());
        return;
    }
    {
        // 能力快照和状态由同一 dataMutex_ 保护，查询线程不会看到半初始化内容。
        std::lock_guard<std::mutex> lock(dataMutex_);
        capabilities_ = connected.takeValue();
    }
    transition(DeviceState::Idle);
    dispatcher_->postConnectCompleted(command.commandId);
}

void VnaSessionController::processPrepare(Command command)
{
    const DeviceState stableState = state();
    // Prepared 允许重新准备；若新计划失败，旧配置及其代次继续有效。
    if ((stableState != DeviceState::Idle) && (stableState != DeviceState::Prepared)) {
        fail(command.commandId, commandError(VnaErrorCode::InvalidState, "prepare requires Idle"));
        return;
    }
    transition(DeviceState::Preparing);

    // 先做无 I/O 的宿主校验，避免把明显非法或过大的输入送入插件。
    const ValidationReport validation = validate(command.plan);
    if (!isValid(validation)) {
        transition(stableState);
        fail(command.commandId, commandError(VnaErrorCode::InvalidPlan, validation.issues.front().description));
        return;
    }
    auto result = adapter_->prepare(command.plan);
    if (!result.hasValue()) {
        transition(stableState);
        fail(command.commandId, result.error());
        return;
    }

    // 只有驱动准备和返回结果校验全部成功后，才发布新的配置代次。
    PreparedSweep prepared = result.takeValue();
    prepared.configurationId = nextConfiguration_++;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        activeConfiguration_ = prepared.configurationId;
        prepared_ = prepared;
        pool_.reset();
    }
    transition(DeviceState::Prepared);
    dispatcher_->postPrepareCompleted(command.commandId, std::move(prepared));
}

void VnaSessionController::processDisconnect(const Command &command)
{
    const DeviceState current = state();
    // 运行中断开会破坏块所有权，因此必须先让扫频进入终态。
    if ((current != DeviceState::Idle) && (current != DeviceState::Prepared) && (current != DeviceState::Fault)) {
        fail(command.commandId, commandError(VnaErrorCode::InvalidState, "disconnect requires an idle device"));
        return;
    }
    transition(DeviceState::Disconnecting);
    VnaError result = adapter_->disconnect();
    if (result.code != VnaErrorCode::None) {
        transition(DeviceState::Fault);
        fail(command.commandId, std::move(result));
        return;
    }
    {
        // 断开后能力和配置均不可继续使用，旧 ConfigurationId 会自然过期。
        std::lock_guard<std::mutex> lock(dataMutex_);
        activeConfiguration_ = 0;
        prepared_ = {};
        capabilities_ = {};
    }
    transition(DeviceState::Disconnected);
}

void VnaSessionController::processWorkerFinished(const Command &command)
{
    // Worker 不直接改状态，只把驱动 Execute 结果重新投递给 Actor 串行收尾。
    finishSweep(command.sweepId, command.workerError);
}

}  // namespace vna::detail
