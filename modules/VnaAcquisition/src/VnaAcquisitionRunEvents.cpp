#include "VnaAcquisitionEngine.h"

#include <optional>
#include <utility>

namespace vna::detail {
namespace {

    bool matchesPendingStart(const std::unique_ptr<AcquisitionActiveRun> &run, const StartCompletedEvent &event)
    {
        if (!run || !run->sweep) {
            return false;
        }
        return !run->sweep->sweepId && (run->sweep->startCommandId == event.commandId);
    }

}  // namespace

void VnaAcquisitionEngine::handleStartCompleted(const StartCompletedEvent &event)
{
    TerminalCallbackTicket ticket;
    std::optional<AcquisitionRunCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool matches = matchesPendingStart(activeRun_, event);
        if (matches && activeRun_->sweep->preStartCompletion) {
            VnaError preStartError = std::move(activeRun_->startTerminalFallback);
            preStartError.code = VnaErrorCode::ProtocolError;
            preStartError.commandId = event.commandId;
            preStartError.sweepId = activeRun_->sweep->preStartCompletion->sweepId;
            clearPreparedForFaultLocked();
            completion = finishFailedRunLocked(std::move(preStartError));
            armTerminalCallbackLocked(ticket);
        } else if (!matches) {
            recordProtocolFaultLocked(makeError(VnaErrorCode::ProtocolError, "unexpected start completion"));
        } else {
            AcquisitionActiveRun &run = *activeRun_;
            run.sweep->sweepId = event.sweepId;
            if (run.abortPending) {
                submitDeferredAbortLocked(run);
            } else if (run.primaryFailure) {
                submitInternalAbortLocked(run);
            }
        }
    }
    if (completion) {
        notifyRunCompleted(std::move(ticket), *completion);
    }
}

void VnaAcquisitionEngine::recordLocalFailureLocked(AcquisitionActiveRun &run, VnaError error)
{
    if (!run.primaryFailure) {
        run.primaryFailure = std::move(error);
    }
    prepared_.reset();
    state_ = AcquisitionState::Fault;
    if (run.sweep) {
        run.sweep->storage.reset();
        submitInternalAbortLocked(run);
    }
}

void VnaAcquisitionEngine::handleWaveBlock(VnaWaveBlock block)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!activeRun_ || !activeRun_->sweep) {
        recordProtocolFaultLocked(makeError(VnaErrorCode::ProtocolError, "wave block arrived without an active sweep"));
        return;
    }
    AcquisitionActiveRun &run = *activeRun_;
    AcquisitionActiveSweep &sweep = *run.sweep;
    const bool wrongSweep = !sweep.sweepId || (*sweep.sweepId != block.sweepId());
    if (wrongSweep || sweep.completion) {
        recordLocalFailureLocked(run, makeError(VnaErrorCode::ProtocolError, "unexpected wave block"));
        return;
    }
    if (run.abortPending || run.primaryFailure || !sweep.storage) {
        return;
    }
    VnaError copied = copyBlockIntoFrame(*run.prepared, block, *sweep.storage);
    if (copied.code != VnaErrorCode::None) {
        recordLocalFailureLocked(run, std::move(copied));
    }
}

void VnaAcquisitionEngine::handleSweepCompleted(const SweepCompletion &event)
{
    AcquisitionRunId runId = 0;
    bool abortTerminals = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeRun_ || !activeRun_->sweep) {
            recordProtocolFaultLocked(
                makeError(VnaErrorCode::ProtocolError, "sweep completion arrived without an active sweep"));
            return;
        }
        AcquisitionActiveSweep &sweep = *activeRun_->sweep;
        runId = activeRun_->runId;
        if (!sweep.sweepId) {
            if (sweep.preStartCompletion) {
                recordProtocolFaultLocked(
                    makeError(VnaErrorCode::ProtocolError, "duplicate pre-start sweep completion"));
            } else {
                sweep.preStartCompletion = event;
                sweep.storage.reset();
            }
            return;
        }
        if ((*sweep.sweepId != event.sweepId) || sweep.completion) {
            recordLocalFailureLocked(
                *activeRun_, makeError(VnaErrorCode::ProtocolError, "unexpected sweep completion"));
            return;
        }
        sweep.completion = event;
        abortTerminals = sweep.abort.commandId != 0;
    }
    if (abortTerminals) {
        processAbortTerminals(runId);
    } else {
        processKnownSweepCompletion(runId);
    }
}

bool VnaAcquisitionEngine::completedFrameIsValid(const AcquisitionActiveRun &run) const
{
    const AcquisitionActiveSweep &sweep = *run.sweep;
    if (!sweep.completion || !sweep.storage || run.abortPending || run.primaryFailure) {
        return false;
    }
    const SweepCompletion &completion = *sweep.completion;
    const RawFrameAssembly &storage = *sweep.storage;
    const PreparedAcquisition &prepared = *run.prepared;
    return (completion.reason == SweepTerminationReason::Completed) &&
           (completion.expectedFrameCount == prepared.pointExcitationRecordCount()) &&
           (completion.receivedFrameCount == completion.expectedFrameCount) &&
           (storage.copiedPointExcitationRecordCount == completion.expectedFrameCount) &&
           (storage.copiedWaveSampleCount == prepared.waveSampleCountPerSweep()) &&
           rawFrameAssemblyComplete(prepared, storage);
}

}  // namespace vna::detail
