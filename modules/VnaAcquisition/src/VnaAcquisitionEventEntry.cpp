#include "VnaAcquisitionEngine.h"

#include <cstdio>
#include <optional>
#include <utility>

namespace vna::detail {

void VnaAcquisitionEngine::onStateChanged(const StateChangedEvent &event)
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        hardwareState_ = event.current;
        if (event.current == DeviceState::Fault) {
            state_ = AcquisitionState::Fault;
            if (!pendingPrepare_ && !activeRun_) {
                prepared_.reset();
            }
        }
    } catch (...) {
        handleUnexpectedException("onStateChanged");
    }
}

void VnaAcquisitionEngine::onConnectCompleted(const ConnectCompletedEvent &)
{
    handleProtocolFault("unexpected connect completion");
}

void VnaAcquisitionEngine::onPrepareCompleted(const PrepareCompletedEvent &event)
{
    try {
        handlePrepareCompleted(event);
    } catch (...) {
        handleUnexpectedException("onPrepareCompleted");
    }
}

void VnaAcquisitionEngine::onStartCompleted(const StartCompletedEvent &event)
{
    try {
        handleStartCompleted(event);
    } catch (...) {
        handleUnexpectedException("onStartCompleted");
    }
}

void VnaAcquisitionEngine::onAbortCompleted(const AbortCompletedEvent &event)
{
    try {
        handleAbortCompleted(event);
    } catch (...) {
        handleUnexpectedException("onAbortCompleted");
    }
}

void VnaAcquisitionEngine::onDisconnectCompleted(const DisconnectCompletedEvent &)
{
    handleProtocolFault("unexpected disconnect completion");
}

void VnaAcquisitionEngine::onWaveBlock(VnaWaveBlock block)
{
    try {
        handleWaveBlock(std::move(block));
    } catch (...) {
        handleUnexpectedException("onWaveBlock");
    }
}

void VnaAcquisitionEngine::onSweepCompleted(const SweepCompletion &event)
{
    try {
        handleSweepCompleted(event);
    } catch (...) {
        handleUnexpectedException("onSweepCompleted");
    }
}

void VnaAcquisitionEngine::onCommandFailed(const CommandFailedEvent &event)
{
    try {
        handleCommandFailed(event);
    } catch (...) {
        handleUnexpectedException("onCommandFailed");
    }
}

void VnaAcquisitionEngine::recordProtocolFaultLocked(VnaError error)
{
    prepared_.reset();
    state_ = AcquisitionState::Fault;
    if (activeRun_) {
        recordLocalFailureLocked(*activeRun_, std::move(error));
    }
}

bool VnaAcquisitionEngine::activeSweepDrainedLocked() const
{
    if (!activeRun_ || !activeRun_->sweep || !activeRun_->sweep->completion) {
        return false;
    }
    const AcquisitionAbortLatch &abort = activeRun_->sweep->abort;
    return (abort.commandId == 0) || abort.commandFinished;
}

void VnaAcquisitionEngine::handleProtocolFault(const char *description) noexcept
{
    TerminalCallbackTicket prepareTicket;
    TerminalCallbackTicket runTicket;
    std::optional<CommandId> prepareCommand;
    std::optional<AcquisitionRunCompletion> runCompletion;
    try {
        auto prepareFailure = Result<std::shared_ptr<const PreparedAcquisition>>::failure(
            makeError(VnaErrorCode::ProtocolError, description));
        VnaError runFailure = makeError(VnaErrorCode::ProtocolError, description);
        VnaError stateFailure = makeError(VnaErrorCode::ProtocolError, description);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pendingPrepare_) {
                prepareCommand = pendingPrepare_->commandId;
                pendingPrepare_.reset();
            }
            recordProtocolFaultLocked(std::move(stateFailure));
            const bool betweenSweeps = activeRun_ && !activeRun_->sweep;
            if (betweenSweeps || activeSweepDrainedLocked()) {
                runCompletion = finishFailedRunLocked(std::move(runFailure));
            }
            if (prepareCommand) {
                armTerminalCallbackLocked(prepareTicket);
            }
            if (runCompletion) {
                armTerminalCallbackLocked(runTicket);
            }
        }
        if (prepareCommand) {
            notifyPrepared(std::move(prepareTicket), *prepareCommand, std::move(prepareFailure));
        }
        if (runCompletion) {
            notifyRunCompleted(std::move(runTicket), *runCompletion);
        }
    } catch (...) {
        std::fputs("VnaAcquisition failed to record a protocol fault\n", stderr);
    }
}

void VnaAcquisitionEngine::handleUnexpectedException(const char *context) noexcept
{
    std::fputs("VnaAcquisition event handler failed: ", stderr);
    std::fputs(context, stderr);
    std::fputc('\n', stderr);
    handleProtocolFault("unhandled acquisition event exception");
}

}  // namespace vna::detail
