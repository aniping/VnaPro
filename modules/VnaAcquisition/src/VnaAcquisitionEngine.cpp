#include "VnaAcquisitionEngine.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace vna {
namespace {

    VnaError factoryError(VnaErrorCode code, const char *description)
    {
        VnaError error;
        error.code = code;
        error.description = description;
        return error;
    }

}  // namespace

Result<std::unique_ptr<IVnaAcquisitionEngine>> createAcquisitionEngine(
    std::shared_ptr<IVnaHardware> hardware, IVnaAcquisitionSink &sink, AcquisitionLimits limits)
{
    if (!hardware) {
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
            factoryError(VnaErrorCode::InvalidState, "hardware is required"));
    }
    if ((limits.maximumRawFramePayloadBytes == 0) || (limits.maximumPreparedMappingPayloadBytes == 0)) {
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
            factoryError(VnaErrorCode::InvalidPlan, "acquisition payload limits must be nonzero"));
    }
    try {
        if (hardware->state() != DeviceState::Idle) {
            return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
                factoryError(VnaErrorCode::InvalidState, "hardware must be connected and idle"));
        }
        const bool supportsAbort = hardware->capabilities().supportsAbort;
        auto implementation = std::make_unique<detail::VnaAcquisitionEngine>(hardware, sink, limits, supportsAbort);
        implementation->subscribe();
        std::unique_ptr<IVnaAcquisitionEngine> result = std::move(implementation);
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::success(std::move(result));
    } catch (const std::logic_error &error) {
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
            factoryError(VnaErrorCode::InvalidState, error.what()));
    } catch (const std::exception &error) {
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
            factoryError(VnaErrorCode::InternalError, error.what()));
    } catch (...) {
        return Result<std::unique_ptr<IVnaAcquisitionEngine>>::failure(
            factoryError(VnaErrorCode::InternalError, "acquisition engine creation failed"));
    }
}

}  // namespace vna

namespace vna::detail {

VnaAcquisitionEngine::VnaAcquisitionEngine(
    std::shared_ptr<IVnaHardware> hardware, IVnaAcquisitionSink &sink, AcquisitionLimits limits, bool supportsAbort)
    : hardware_(std::move(hardware)), sink_(&sink), limits_(limits), supportsAbort_(supportsAbort)
{
}

VnaAcquisitionEngine::TerminalCallbackTicket::TerminalCallbackTicket() noexcept = default;

VnaAcquisitionEngine::TerminalCallbackTicket::~TerminalCallbackTicket() noexcept
{
    if (engine_ != nullptr) {
        engine_->releaseTerminalCallbackDelivery();
    }
}

VnaAcquisitionEngine::TerminalCallbackTicket::TerminalCallbackTicket(TerminalCallbackTicket &&other) noexcept
    : engine_(std::exchange(other.engine_, nullptr))
{
}

VnaAcquisitionEngine::~VnaAcquisitionEngine()
{
    subscription_ = Subscription{};
    std::lock_guard<std::mutex> lock(mutex_);
    if (pendingPrepare_ || activeRun_) {
        std::fputs("VnaAcquisition destroyed with an active operation\n", stderr);
    }
}

void VnaAcquisitionEngine::subscribe()
{
    subscription_ = hardware_->subscribe(*this);
}

AcquisitionState VnaAcquisitionEngine::state() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    waitForTerminalCallbackDeliveryLocked(lock);
    return state_;
}

VnaError VnaAcquisitionEngine::makeError(VnaErrorCode code, const char *description)
{
    VnaError error;
    error.code = code;
    error.description = description;
    return error;
}

VnaError VnaAcquisitionEngine::invalidStateError(const char *description)
{
    return makeError(VnaErrorCode::InvalidState, description);
}

void VnaAcquisitionEngine::notifyPrepared(
    TerminalCallbackTicket, CommandId commandId, Result<std::shared_ptr<const PreparedAcquisition>> outcome) noexcept
{
    try {
        beginTerminalCallbackDelivery();
        try {
            sink_->onPrepared(commandId, std::move(outcome));
        } catch (...) {
            logCallbackException("onPrepared");
        }
    } catch (...) {
        logCallbackException("onPrepared delivery gate");
    }
}

void VnaAcquisitionEngine::notifySweepReady(std::shared_ptr<const RawSweepFrame> frame) noexcept
{
    try {
        sink_->onSweepReady(std::move(frame));
    } catch (...) {
        logCallbackException("onSweepReady");
    }
}

void VnaAcquisitionEngine::notifyRunCompleted(
    TerminalCallbackTicket, const AcquisitionRunCompletion &completion) noexcept
{
    try {
        beginTerminalCallbackDelivery();
        try {
            sink_->onRunCompleted(completion);
        } catch (...) {
            logCallbackException("onRunCompleted");
        }
    } catch (...) {
        logCallbackException("onRunCompleted delivery gate");
    }
}

void VnaAcquisitionEngine::logCallbackException(const char *callbackName) const noexcept
{
    std::fputs("VnaAcquisition sink callback threw: ", stderr);
    std::fputs(callbackName, stderr);
    std::fputc('\n', stderr);
}

AcquisitionState VnaAcquisitionEngine::stableStateLocked() const
{
    if ((state_ == AcquisitionState::Fault) || (hardwareState_ == DeviceState::Fault)) {
        return AcquisitionState::Fault;
    }
    return prepared_ ? AcquisitionState::Ready : AcquisitionState::Unprepared;
}

void VnaAcquisitionEngine::clearPreparedForFaultLocked()
{
    prepared_.reset();
    state_ = AcquisitionState::Fault;
}

void VnaAcquisitionEngine::waitForTerminalCallbackDeliveryLocked(std::unique_lock<std::mutex> &lock) const
{
    terminalCallbackCondition_.wait(lock, [this] {
        return (pendingTerminalCallbackCount_ == 0) || (terminalCallbackThread_ == std::this_thread::get_id());
    });
}

void VnaAcquisitionEngine::armTerminalCallbackLocked(TerminalCallbackTicket &ticket) noexcept
{
    ++pendingTerminalCallbackCount_;
    ticket.engine_ = this;
}

void VnaAcquisitionEngine::beginTerminalCallbackDelivery()
{
    std::lock_guard<std::mutex> lock(mutex_);
    terminalCallbackThread_ = std::this_thread::get_id();
}

void VnaAcquisitionEngine::releaseTerminalCallbackDelivery() noexcept
{
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pendingTerminalCallbackCount_ > 0) {
            --pendingTerminalCallbackCount_;
        }
        terminalCallbackThread_ = std::thread::id{};
    } catch (...) {
        std::fputs("VnaAcquisition failed to release a terminal callback gate\n", stderr);
    }
    terminalCallbackCondition_.notify_all();
}

AcquisitionRunCompletion VnaAcquisitionEngine::finishRunLocked(
    AcquisitionRunTermination termination, std::optional<VnaError> error)
{
    AcquisitionRunCompletion completion;
    completion.runId = activeRun_->runId;
    completion.termination = termination;
    completion.completedSweepCount = activeRun_->completedSweepCount;
    completion.error = std::move(error);
    activeRun_.reset();
    state_ = stableStateLocked();
    return completion;
}

AcquisitionRunCompletion VnaAcquisitionEngine::finishFailedRunLocked(VnaError error)
{
    if (hardwareState_ == DeviceState::Fault) {
        clearPreparedForFaultLocked();
    }
    return finishRunLocked(AcquisitionRunTermination::Failed, std::move(error));
}

}  // namespace vna::detail
