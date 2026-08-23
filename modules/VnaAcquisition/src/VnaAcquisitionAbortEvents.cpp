#include "VnaAcquisitionEngine.h"

#include <optional>
#include <utility>

namespace vna::detail {
namespace {

    VnaError failedAbortError(const AcquisitionActiveSweep &sweep)
    {
        if (!sweep.abort.commandSucceeded) {
            return sweep.abort.commandError;
        }
        VnaError error;
        error.code = VnaErrorCode::ProtocolError;
        error.sweepId = sweep.sweepId.value_or(0);
        error.description = "abort command did not end with an aborted sweep";
        return error;
    }

    bool faultSweepTermination(SweepTerminationReason reason)
    {
        return (reason != SweepTerminationReason::Completed) && (reason != SweepTerminationReason::Aborted);
    }

    bool successfulUserAbort(const AcquisitionActiveSweep &sweep, DeviceState hardwareState)
    {
        return (sweep.abort.kind == AcquisitionAbortKind::User) && sweep.abort.commandSucceeded &&
               (sweep.completion->reason == SweepTerminationReason::Aborted) &&
               (hardwareState == DeviceState::Prepared);
    }

}  // namespace

void VnaAcquisitionEngine::handleAbortCompleted(const AbortCompletedEvent &event)
{
    AcquisitionRunId runId = 0;
    bool matched = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeRun_ && activeRun_->sweep) {
            AcquisitionActiveSweep &sweep = *activeRun_->sweep;
            matched = (sweep.abort.commandId == event.commandId) && sweep.sweepId &&
                      (*sweep.sweepId == event.sweepId) && !sweep.abort.commandFinished;
            if (matched) {
                sweep.abort.commandFinished = true;
                sweep.abort.commandSucceeded = true;
                runId = activeRun_->runId;
            }
        }
    }
    if (!matched) {
        handleProtocolFault("unknown abort completion");
        return;
    }
    processAbortTerminals(runId);
}

void VnaAcquisitionEngine::processAbortTerminals(AcquisitionRunId runId)
{
    TerminalCallbackTicket ticket;
    std::optional<AcquisitionRunCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeRun_ || (activeRun_->runId != runId) || !activeRun_->sweep) {
            return;
        }
        AcquisitionActiveRun &run = *activeRun_;
        AcquisitionActiveSweep &sweep = *run.sweep;
        if (!sweep.completion || !sweep.abort.commandFinished) {
            return;
        }
        completion = finishAbortTerminalsLocked(run);
        armTerminalCallbackLocked(ticket);
    }
    if (completion) {
        notifyRunCompleted(std::move(ticket), *completion);
    }
}

AcquisitionRunCompletion VnaAcquisitionEngine::finishAbortTerminalsLocked(AcquisitionActiveRun &run)
{
    const AcquisitionActiveSweep &sweep = *run.sweep;
    if (run.primaryFailure) {
        return finishFailedRunLocked(*run.primaryFailure);
    }
    if (successfulUserAbort(sweep, hardwareState_)) {
        return finishRunLocked(AcquisitionRunTermination::Aborted, std::nullopt);
    }
    VnaError error = failedAbortError(sweep);
    if (faultSweepTermination(sweep.completion->reason) || (hardwareState_ == DeviceState::Fault)) {
        clearPreparedForFaultLocked();
    }
    return finishFailedRunLocked(std::move(error));
}

void VnaAcquisitionEngine::handleAbortFailed(const CommandFailedEvent &event)
{
    AcquisitionRunId runId = 0;
    bool matched = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeRun_ && activeRun_->sweep) {
            AcquisitionAbortLatch &abort = activeRun_->sweep->abort;
            matched = (abort.commandId == event.commandId) && !abort.commandFinished;
            if (matched) {
                abort.commandFinished = true;
                abort.commandSucceeded = false;
                abort.commandError = event.error;
                runId = activeRun_->runId;
            }
        }
    }
    if (!matched) {
        handleProtocolFault("unknown abort command failure");
        return;
    }
    processAbortTerminals(runId);
}

}  // namespace vna::detail
