#include "VnaAcquisitionEngine.h"

#include <optional>
#include <utility>

namespace vna::detail {
namespace {

    enum class CommandFailureMatch : std::uint8_t { None, Prepare, Start, Abort };

    bool matchesPrepare(const std::optional<AcquisitionPendingPrepare> &pending, CommandId commandId)
    {
        return pending && (pending->commandId == commandId);
    }

    bool matchesStart(const std::unique_ptr<AcquisitionActiveRun> &run, CommandId commandId)
    {
        return run && run->sweep && !run->sweep->sweepId && (run->sweep->startCommandId == commandId);
    }

    bool matchesAbort(const std::unique_ptr<AcquisitionActiveRun> &run, CommandId commandId)
    {
        return run && run->sweep && (run->sweep->abort.commandId == commandId) && !run->sweep->abort.commandFinished;
    }

    CommandFailureMatch classifyCommandFailure(
        const std::optional<AcquisitionPendingPrepare> &pending, const std::unique_ptr<AcquisitionActiveRun> &run,
        CommandId commandId)
    {
        if (matchesPrepare(pending, commandId)) {
            return CommandFailureMatch::Prepare;
        }
        if (matchesStart(run, commandId)) {
            return CommandFailureMatch::Start;
        }
        return matchesAbort(run, commandId) ? CommandFailureMatch::Abort : CommandFailureMatch::None;
    }

}  // namespace

void VnaAcquisitionEngine::handleCommandFailed(const CommandFailedEvent &event)
{
    CommandFailureMatch match = CommandFailureMatch::None;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        match = classifyCommandFailure(pendingPrepare_, activeRun_, event.commandId);
    }
    switch (match) {
        case CommandFailureMatch::Prepare:
            handlePrepareFailed(event);
            return;
        case CommandFailureMatch::Start:
            handleStartFailed(event);
            return;
        case CommandFailureMatch::Abort:
            handleAbortFailed(event);
            return;
        case CommandFailureMatch::None:
            handleProtocolFault("unknown command failure");
            return;
    }
}

void VnaAcquisitionEngine::handleStartFailed(const CommandFailedEvent &event)
{
    TerminalCallbackTicket ticket;
    std::optional<AcquisitionRunCompletion> completion;
    bool matched = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        matched = activeRun_ && activeRun_->sweep && !activeRun_->sweep->sweepId &&
                  (activeRun_->sweep->startCommandId == event.commandId);
        if (matched) {
            VnaError failure = startCommandFailureLocked(*activeRun_, event.error);
            completion = finishFailedRunLocked(std::move(failure));
            armTerminalCallbackLocked(ticket);
        }
    }
    if (!matched) {
        handleProtocolFault("start command context changed before failure");
        return;
    }
    notifyRunCompleted(std::move(ticket), *completion);
}

VnaError VnaAcquisitionEngine::startCommandFailureLocked(
    AcquisitionActiveRun &run, const VnaError &reportedError) noexcept
{
    if (run.primaryFailure) {
        return std::move(*run.primaryFailure);
    }
    try {
        return reportedError;
    } catch (...) {
        run.startTerminalFallback.commandId = reportedError.commandId;
        return std::move(run.startTerminalFallback);
    }
}

}  // namespace vna::detail
