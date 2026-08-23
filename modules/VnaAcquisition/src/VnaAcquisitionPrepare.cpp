#include "VnaAcquisitionEngine.h"

#include <exception>
#include <optional>
#include <utility>

namespace vna::detail {
namespace {

    VnaError validationError(const ValidationReport &report)
    {
        VnaError error;
        error.code = VnaErrorCode::InvalidPlan;
        error.description = report.issues.empty() ? "hardware rejected the plan" : report.issues.front().description;
        return error;
    }

    bool matchesPendingPrepare(const std::optional<AcquisitionPendingPrepare> &pending, CommandId commandId)
    {
        return pending && (pending->commandId == commandId);
    }

}  // namespace

Result<CommandId> VnaAcquisitionEngine::prepareAsync(VnaSweepPlan plan)
{
    try {
        auto inspection = inspectRequestedPlan(plan, limits_);
        if (!inspection.hasValue()) {
            return Result<CommandId>::failure(inspection.error());
        }
        std::unique_lock<std::mutex> lock(mutex_);
        waitForTerminalCallbackDeliveryLocked(lock);
        if ((state_ != AcquisitionState::Unprepared) && (state_ != AcquisitionState::Ready)) {
            return Result<CommandId>::failure(invalidStateError("acquisition cannot prepare in its current state"));
        }
        const ValidationReport report = hardware_->validate(plan);
        if (!isValid(report)) {
            return Result<CommandId>::failure(validationError(report));
        }
        const AcquisitionState previousState = state_;
        const auto previousPrepared = prepared_;
        state_ = AcquisitionState::Preparing;
        try {
            auto submitted = hardware_->prepareAsync(std::move(plan));
            if (!submitted.hasValue()) {
                state_ = previousState;
                prepared_ = previousPrepared;
                return Result<CommandId>::failure(submitted.error());
            }
            const CommandId commandId = submitted.value();
            pendingPrepare_ = AcquisitionPendingPrepare{commandId, previousState, previousPrepared};
            return Result<CommandId>::success(commandId);
        } catch (...) {
            state_ = previousState;
            prepared_ = previousPrepared;
            throw;
        }
    } catch (const std::exception &error) {
        return Result<CommandId>::failure(makeError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<CommandId>::failure(makeError(VnaErrorCode::InternalError, "hardware prepare submission failed"));
    }
}

void VnaAcquisitionEngine::handlePrepareCompleted(const PrepareCompletedEvent &event)
{
    bool unknown = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        unknown = !matchesPendingPrepare(pendingPrepare_, event.commandId);
    }
    if (unknown) {
        handleProtocolFault("unknown prepare completion");
        return;
    }
    auto outcome = compilePreparedAcquisition(event.prepared, limits_);
    std::shared_ptr<const PreparedAcquisition> result;
    VnaError failure;
    if (outcome.hasValue() && outcome.value()) {
        result = outcome.takeValue();
    } else {
        failure = outcome.hasValue() ? makeError(VnaErrorCode::ProtocolError, "prepared acquisition is empty")
                                     : outcome.error();
    }
    auto notification = result ? Result<std::shared_ptr<const PreparedAcquisition>>::success(result)
                               : Result<std::shared_ptr<const PreparedAcquisition>>::failure(std::move(failure));
    TerminalCallbackTicket ticket;
    bool contextChanged = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!matchesPendingPrepare(pendingPrepare_, event.commandId)) {
            contextChanged = true;
        } else if (result) {
            prepared_ = result;
            state_ = AcquisitionState::Ready;
            pendingPrepare_.reset();
        } else {
            prepared_.reset();
            state_ = AcquisitionState::Unprepared;
            pendingPrepare_.reset();
        }
        if (!contextChanged) {
            armTerminalCallbackLocked(ticket);
        }
    }
    if (contextChanged) {
        handleProtocolFault("prepare context changed during compilation");
        return;
    }
    notifyPrepared(std::move(ticket), event.commandId, std::move(notification));
}

void VnaAcquisitionEngine::settleFailedPrepareLocked()
{
    if ((hardwareState_ == DeviceState::Fault) || (state_ == AcquisitionState::Fault)) {
        pendingPrepare_.reset();
        clearPreparedForFaultLocked();
    } else if (pendingPrepare_->previousPrepared && (hardwareState_ == DeviceState::Prepared)) {
        prepared_ = pendingPrepare_->previousPrepared;
        pendingPrepare_.reset();
        state_ = AcquisitionState::Ready;
    } else {
        prepared_.reset();
        pendingPrepare_.reset();
        state_ = AcquisitionState::Unprepared;
    }
}

void VnaAcquisitionEngine::handlePrepareFailed(const CommandFailedEvent &event)
{
    auto notification = Result<std::shared_ptr<const PreparedAcquisition>>::failure(event.error);
    TerminalCallbackTicket ticket;
    bool matched = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        matched = matchesPendingPrepare(pendingPrepare_, event.commandId);
        if (matched) {
            settleFailedPrepareLocked();
            armTerminalCallbackLocked(ticket);
        }
    }
    if (!matched) {
        handleProtocolFault("unknown prepare command failure");
        return;
    }
    notifyPrepared(std::move(ticket), event.commandId, std::move(notification));
}

}  // namespace vna::detail
