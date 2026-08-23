#include "VnaAcquisitionEngine.h"

#include <optional>
#include <utility>

namespace vna::detail {
namespace {

    VnaError errorForSweepCompletion(const SweepCompletion &completion)
    {
        VnaError error;
        error.sweepId = completion.sweepId;
        switch (completion.reason) {
            case SweepTerminationReason::Completed:
                error.code = VnaErrorCode::ProtocolError;
                error.description = "completed sweep did not form a complete raw frame";
                break;
            case SweepTerminationReason::Aborted:
                error.code = VnaErrorCode::ProtocolError;
                error.description = "sweep aborted without a matching acquisition abort";
                break;
            case SweepTerminationReason::ConnectionLost:
                error.code = VnaErrorCode::ConnectionLost;
                error.description = "hardware connection was lost during acquisition";
                break;
            case SweepTerminationReason::Timeout:
                error.code = VnaErrorCode::Timeout;
                error.description = "hardware sweep timed out";
                break;
            case SweepTerminationReason::DataLoss:
                error.code = VnaErrorCode::DataLoss;
                error.description = "hardware reported incomplete sweep data";
                break;
            case SweepTerminationReason::HardwareFault:
                error.code = VnaErrorCode::HardwareFault;
                error.description = "hardware sweep failed";
                break;
        }
        return error;
    }

    AcquisitionActiveRun *completedRunFor(
        std::unique_ptr<AcquisitionActiveRun> &activeRun, AcquisitionRunId runId) noexcept
    {
        if (!activeRun || (activeRun->runId != runId) || !activeRun->sweep || !activeRun->sweep->completion) {
            return nullptr;
        }
        return activeRun.get();
    }

    AcquisitionActiveRun *betweenSweepsRunFor(
        std::unique_ptr<AcquisitionActiveRun> &activeRun, AcquisitionRunId runId) noexcept
    {
        if (!activeRun || (activeRun->runId != runId) || activeRun->sweep) {
            return nullptr;
        }
        return activeRun.get();
    }

}  // namespace

void VnaAcquisitionEngine::processKnownSweepCompletion(AcquisitionRunId runId)
{
    TerminalCallbackTicket ticket;
    bool publish = false;
    std::optional<AcquisitionRunCompletion> runCompletion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        AcquisitionActiveRun *run = completedRunFor(activeRun_, runId);
        if (run == nullptr) {
            return;
        }
        const SweepCompletion completion = *run->sweep->completion;
        if (run->sweep->abort.commandId != 0) {
            return;
        }
        if (run->primaryFailure) {
            runCompletion = finishFailedRunLocked(*run->primaryFailure);
        } else if (completedFrameIsValid(*run)) {
            publish = true;
        } else {
            VnaError error = errorForSweepCompletion(completion);
            clearPreparedForFaultLocked();
            runCompletion = finishFailedRunLocked(std::move(error));
        }
        if (runCompletion) {
            armTerminalCallbackLocked(ticket);
        }
    }
    if (publish) {
        publishCompletedSweep(runId);
    } else if (runCompletion) {
        notifyRunCompleted(std::move(ticket), *runCompletion);
    }
}

void VnaAcquisitionEngine::publishCompletedSweep(AcquisitionRunId runId)
{
    TerminalCallbackTicket ticket;
    std::shared_ptr<const RawSweepFrame> frame;
    std::optional<AcquisitionRunCompletion> failure;
    bool finiteLast = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeRun_ || (activeRun_->runId != runId) || !completedFrameIsValid(*activeRun_)) {
            return;
        }
        AcquisitionActiveRun &run = *activeRun_;
        AcquisitionActiveSweep &sweep = *run.sweep;
        auto frozen =
            freezeRawSweepFrame(run.runId, sweep.sweepIndex, *sweep.sweepId, run.prepared, std::move(*sweep.storage));
        if (!frozen.hasValue()) {
            clearPreparedForFaultLocked();
            failure = finishFailedRunLocked(frozen.error());
        } else {
            frame = frozen.takeValue();
            ++run.completedSweepCount;
            finiteLast =
                (run.request.mode == AcquisitionRunMode::Finite) && (run.completedSweepCount >= run.request.sweepCount);
            run.closing = finiteLast;
            run.sweep.reset();
        }
        if (failure) {
            armTerminalCallbackLocked(ticket);
        }
    }
    if (failure) {
        notifyRunCompleted(std::move(ticket), *failure);
        return;
    }
    notifySweepReady(std::move(frame));
    afterSweepPublished(runId, finiteLast);
}

void VnaAcquisitionEngine::afterSweepPublished(AcquisitionRunId runId, bool finiteLast)
{
    TerminalCallbackTicket ticket;
    std::optional<AcquisitionRunCompletion> completion;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        AcquisitionActiveRun *run = betweenSweepsRunFor(activeRun_, runId);
        if (run == nullptr) {
            return;
        }
        if (finiteLast) {
            completion = finishRunLocked(AcquisitionRunTermination::Completed, std::nullopt);
        } else if (run->abortPending || run->abortAfterSweep) {
            completion = finishRunLocked(AcquisitionRunTermination::Aborted, std::nullopt);
        } else if (run->stopPending) {
            completion = finishRunLocked(AcquisitionRunTermination::Stopped, std::nullopt);
        } else {
            VnaError error;
            if (!submitNextSweepLocked(*run, error)) {
                completion = finishFailedRunLocked(std::move(error));
            }
        }
        if (completion) {
            armTerminalCallbackLocked(ticket);
        }
    }
    if (completion) {
        notifyRunCompleted(std::move(ticket), *completion);
    }
}

}  // namespace vna::detail
