#include "VnaSessionController.h"

#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

#include "VnaPreparedCapacity.h"

namespace vna::detail {
namespace {

    VnaError admissionError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

}  // namespace

Result<CommandId> VnaSessionController::enqueueExternal(Command command, bool reservedControlSlot)
{
    try {
        std::lock_guard<std::mutex> lock(commandMutex_);
        const std::size_t normalCapacity = limits_.maximumPendingCommandsPerDevice - 1;
        const bool commandCapacityAvailable =
            outstandingExternalCommands_ <
            (reservedControlSlot ? limits_.maximumPendingCommandsPerDevice : normalCapacity);
        const bool payloadCapacityAvailable =
            (outstandingPreparePayloadBytes_ <= limits_.maximumPendingPreparePayloadBytesPerDevice) &&
            (command.planPayloadBytes <=
             (limits_.maximumPendingPreparePayloadBytesPerDevice - outstandingPreparePayloadBytes_));
        if (actorStopping_) {
            return Result<CommandId>::failure(
                admissionError(VnaErrorCode::InvalidState, "hardware session is shutting down"));
        }
        if (!commandCapacityAvailable || !payloadCapacityAvailable) {
            return Result<CommandId>::failure(
                admissionError(VnaErrorCode::ResourceExhausted, "hardware command admission limit is exhausted"));
        }
        if (nextCommand_.load() == std::numeric_limits<CommandId>::max()) {
            return Result<CommandId>::failure(
                admissionError(VnaErrorCode::ResourceExhausted, "hardware command identifier is exhausted"));
        }
        command.commandId = nextCommand_.fetch_add(1);
        const CommandId identifier = command.commandId;
        const std::size_t planPayloadBytes = command.planPayloadBytes;
        commandSlots_[commandTail_].emplace(std::move(command));
        commandTail_ = (commandTail_ + 1) % commandSlots_.size();
        ++queuedExternalCommands_;
        ++outstandingExternalCommands_;
        outstandingPreparePayloadBytes_ += planPayloadBytes;
        commandReady_.notify_one();
        return Result<CommandId>::success(identifier);
    } catch (const std::exception &error) {
        return Result<CommandId>::failure(admissionError(VnaErrorCode::ResourceExhausted, error.what()));
    } catch (...) {
        return Result<CommandId>::failure(
            admissionError(VnaErrorCode::ResourceExhausted, "hardware command admission failed"));
    }
}

Result<CommandId> VnaSessionController::connectAsync()
{
    Command command;
    command.kind = CommandKind::Connect;
    return enqueueExternal(std::move(command), false);
}

Result<CommandId> VnaSessionController::prepareAsync(VnaSweepPlan plan)
{
    auto payload = inspectRequestedPlanPayload(plan, limits_.maximumRequestedPlanPayloadBytes);
    if (!payload.hasValue()) {
        return Result<CommandId>::failure(payload.error());
    }
    Command command;
    command.kind = CommandKind::Prepare;
    command.planPayloadBytes = payload.value();
    command.plan = std::move(plan);
    return enqueueExternal(std::move(command), false);
}

Result<CommandId> VnaSessionController::startAsync(ConfigurationId configurationId)
{
    Command command;
    command.kind = CommandKind::Start;
    command.configurationId = configurationId;
    return enqueueExternal(std::move(command), false);
}

Result<CommandId> VnaSessionController::abortAsync(SweepId sweepId)
{
    Command command;
    command.kind = CommandKind::Abort;
    command.sweepId = sweepId;
    return enqueueExternal(std::move(command), true);
}

Result<CommandId> VnaSessionController::disconnectAsync()
{
    Command command;
    command.kind = CommandKind::Disconnect;
    return enqueueExternal(std::move(command), true);
}

void VnaSessionController::enqueueWorkerCompletion(Command command)
{
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        if (actorStopping_) {
            return;
        }
        // 同 commandMutex_ 下的 ID 边界精确区分完成消息前后准入的外部命令。
        command.completionBeforeCommandId = nextCommand_.load();
        workerCompletion_ = std::move(command);
    }
    commandReady_.notify_one();
}

void VnaSessionController::releaseExternalReservation(std::size_t planPayloadBytes) noexcept
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    --outstandingExternalCommands_;
    outstandingPreparePayloadBytes_ -= planPayloadBytes;
}

}  // namespace vna::detail
