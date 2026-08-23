#include "VnaAcquisitionEngine.h"

#include <exception>
#include <limits>
#include <utility>

namespace vna::detail {
namespace {

    bool validRunRequest(AcquisitionRunRequest request)
    {
        if (request.mode == AcquisitionRunMode::Continuous) {
            return true;
        }
        return (request.mode == AcquisitionRunMode::Finite) && (request.sweepCount > 0);
    }

    bool terminalAllowsNextSweepRequest(const AcquisitionActiveRun &run)
    {
        if (!run.sweep || !run.sweep->completion || run.primaryFailure || run.abortPending) {
            return false;
        }
        const bool finiteLast = (run.request.mode == AcquisitionRunMode::Finite) &&
                                (run.completedSweepCount >= static_cast<std::uint64_t>(run.request.sweepCount - 1));
        return (run.sweep->completion->reason == SweepTerminationReason::Completed) && !finiteLast;
    }

    bool matchesOpenRun(
        AcquisitionState state, const std::unique_ptr<AcquisitionActiveRun> &run, AcquisitionRunId runId)
    {
        return (state == AcquisitionState::Running) && run && (run->runId == runId) && !run->closing;
    }

    bool stopOrAbortAlreadyAccepted(const AcquisitionActiveRun &run)
    {
        return run.abortPending || run.abortAfterSweep || run.stopPending;
    }

}  // namespace

Result<std::unique_ptr<AcquisitionActiveSweep>> VnaAcquisitionEngine::makeSweepLocked(
    std::uint64_t sweepIndex, const std::shared_ptr<const PreparedAcquisition> &prepared) const
{
    auto assembly = allocateRawFrameAssembly(*prepared);
    if (!assembly.hasValue()) {
        return Result<std::unique_ptr<AcquisitionActiveSweep>>::failure(assembly.error());
    }
    try {
        auto sweep = std::make_unique<AcquisitionActiveSweep>();
        sweep->sweepIndex = sweepIndex;
        sweep->storage = std::make_unique<RawFrameAssembly>(assembly.takeValue());
        return Result<std::unique_ptr<AcquisitionActiveSweep>>::success(std::move(sweep));
    } catch (const std::exception &error) {
        return Result<std::unique_ptr<AcquisitionActiveSweep>>::failure(
            makeError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<std::unique_ptr<AcquisitionActiveSweep>>::failure(
            makeError(VnaErrorCode::InternalError, "raw frame allocation failed"));
    }
}

Result<AcquisitionRunId> VnaAcquisitionEngine::start(AcquisitionRunRequest request)
{
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        waitForTerminalCallbackDeliveryLocked(lock);
        return startLocked(request);
    } catch (const std::exception &error) {
        return Result<AcquisitionRunId>::failure(makeError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<AcquisitionRunId>::failure(makeError(VnaErrorCode::InternalError, "acquisition start failed"));
    }
}

Result<AcquisitionRunId> VnaAcquisitionEngine::startLocked(AcquisitionRunRequest request)
{
    if ((state_ != AcquisitionState::Ready) || !prepared_ || activeRun_) {
        return Result<AcquisitionRunId>::failure(invalidStateError("acquisition is not ready"));
    }
    if (!validRunRequest(request)) {
        return Result<AcquisitionRunId>::failure(
            makeError(VnaErrorCode::InvalidPlan, "acquisition run request is invalid"));
    }
    if (nextRunId_ == std::numeric_limits<AcquisitionRunId>::max()) {
        return Result<AcquisitionRunId>::failure(
            makeError(VnaErrorCode::InternalError, "acquisition run identifier exhausted"));
    }
    auto sweep = makeSweepLocked(0, prepared_);
    if (!sweep.hasValue()) {
        return Result<AcquisitionRunId>::failure(sweep.error());
    }
    auto run = std::make_unique<AcquisitionActiveRun>();
    run->runId = nextRunId_;
    run->request = request;
    run->startTerminalFallback = makeError(VnaErrorCode::InternalError, "acquisition start terminal processing failed");
    run->prepared = prepared_;
    run->sweep = sweep.takeValue();
    auto submitted = hardware_->startAsync(prepared_->hardwarePrepared().configurationId);
    if (!submitted.hasValue()) {
        return Result<AcquisitionRunId>::failure(submitted.error());
    }
    run->sweep->startCommandId = submitted.value();
    activeRun_ = std::move(run);
    state_ = AcquisitionState::Running;
    const AcquisitionRunId runId = nextRunId_;
    ++nextRunId_;
    return Result<AcquisitionRunId>::success(runId);
}

VnaError VnaAcquisitionEngine::requestStop(AcquisitionRunId runId)
{
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        waitForTerminalCallbackDeliveryLocked(lock);
        if (!matchesOpenRun(state_, activeRun_, runId)) {
            return invalidStateError("acquisition run cannot be stopped");
        }
        if (stopOrAbortAlreadyAccepted(*activeRun_)) {
            return {};
        }
        if (activeRun_->sweep && activeRun_->sweep->completion && !terminalAllowsNextSweepRequest(*activeRun_)) {
            return invalidStateError("completed sweep is already closing the run");
        }
        activeRun_->stopPending = true;
        return {};
    } catch (const std::exception &error) {
        return makeError(VnaErrorCode::InternalError, error.what());
    } catch (...) {
        return makeError(VnaErrorCode::InternalError, "acquisition stop request failed");
    }
}

VnaError VnaAcquisitionEngine::requestAbort(AcquisitionRunId runId)
{
    try {
        std::unique_lock<std::mutex> lock(mutex_);
        waitForTerminalCallbackDeliveryLocked(lock);
        if (!matchesOpenRun(state_, activeRun_, runId)) {
            return invalidStateError("acquisition run cannot be aborted");
        }
        return requestAbortLocked(*activeRun_);
    } catch (const std::exception &error) {
        return makeError(VnaErrorCode::InternalError, error.what());
    } catch (...) {
        return makeError(VnaErrorCode::InternalError, "acquisition abort submission failed");
    }
}

VnaError VnaAcquisitionEngine::requestAbortLocked(AcquisitionActiveRun &run)
{
    if (run.abortPending || run.abortAfterSweep) {
        return {};
    }
    if (!run.sweep) {
        run.abortAfterSweep = true;
        return {};
    }
    if (run.sweep->completion) {
        if (!terminalAllowsNextSweepRequest(run)) {
            return invalidStateError("completed sweep is already closing the run");
        }
        run.abortAfterSweep = true;
        return {};
    }
    if (!supportsAbort_) {
        return makeError(VnaErrorCode::UnsupportedPlan, "hardware does not support abort");
    }
    if (!run.sweep->sweepId) {
        run.abortPending = true;
        run.sweep->abort.kind = AcquisitionAbortKind::User;
        return {};
    }
    return submitUserAbortLocked(run);
}

VnaError VnaAcquisitionEngine::submitUserAbortLocked(AcquisitionActiveRun &run)
{
    AcquisitionActiveSweep &sweep = *run.sweep;
    auto submitted = hardware_->abortAsync(*sweep.sweepId);
    if (!submitted.hasValue()) {
        return submitted.error();
    }
    run.abortPending = true;
    sweep.abort.kind = AcquisitionAbortKind::User;
    sweep.abort.commandId = submitted.value();
    return {};
}

void VnaAcquisitionEngine::submitDeferredAbortLocked(AcquisitionActiveRun &run)
{
    try {
        VnaError failure = submitUserAbortLocked(run);
        if (failure.code == VnaErrorCode::None) {
            return;
        }
        run.sweep->abort = {};
        recordLocalFailureLocked(run, std::move(failure));
    } catch (const std::exception &error) {
        run.sweep->abort = {};
        recordLocalFailureLocked(run, makeError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        run.sweep->abort = {};
        recordLocalFailureLocked(run, makeError(VnaErrorCode::InternalError, "deferred abort submission failed"));
    }
}

bool VnaAcquisitionEngine::submitInternalAbortLocked(AcquisitionActiveRun &run)
{
    AcquisitionActiveSweep &sweep = *run.sweep;
    if (!supportsAbort_ || !sweep.sweepId || (sweep.abort.kind != AcquisitionAbortKind::None)) {
        return false;
    }
    try {
        auto submitted = hardware_->abortAsync(*sweep.sweepId);
        if (!submitted.hasValue()) {
            return false;
        }
        sweep.abort.commandId = submitted.value();
        sweep.abort.kind = AcquisitionAbortKind::Internal;
        return true;
    } catch (...) {
        return false;
    }
}

bool VnaAcquisitionEngine::submitNextSweepLocked(AcquisitionActiveRun &run, VnaError &error)
{
    if (run.completedSweepCount == std::numeric_limits<std::uint64_t>::max()) {
        error = makeError(VnaErrorCode::InternalError, "acquisition sweep counter exhausted");
        return false;
    }
    auto next = makeSweepLocked(run.completedSweepCount, run.prepared);
    if (!next.hasValue()) {
        error = next.error();
        return false;
    }
    try {
        auto submitted = hardware_->startAsync(run.prepared->hardwarePrepared().configurationId);
        if (!submitted.hasValue()) {
            error = submitted.error();
            return false;
        }
        next.value()->startCommandId = submitted.value();
        run.sweep = next.takeValue();
        return true;
    } catch (const std::exception &exception) {
        error = makeError(VnaErrorCode::InternalError, exception.what());
    } catch (...) {
        error = makeError(VnaErrorCode::InternalError, "next sweep submission failed");
    }
    return false;
}

}  // namespace vna::detail
