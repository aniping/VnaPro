#include "VnaSessionController.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "HostServices.h"
#include "VnaBufferPool.h"
#include "VnaFrameTracker.h"
#include "VnaLogger.h"
#include "VnaSweepTermination.h"

namespace vna::detail {
namespace {

    // SweepId 跨会话唯一，HostServices 才能用一个进程级注册表安全路由回调。
    std::atomic<SweepId> globalSweepIdentifier{1};

    VnaError sweepError(VnaErrorCode code, std::string description)
    {
        VnaError error;
        error.code = code;
        error.description = std::move(description);
        return error;
    }

    SweepId reserveSweepIdentifier() noexcept
    {
        SweepId current = globalSweepIdentifier.load(std::memory_order_relaxed);
        const SweepId maximum = std::numeric_limits<SweepId>::max();
        while (current != maximum) {
            if (globalSweepIdentifier.compare_exchange_weak(
                    current, current + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                return current;
            }
        }
        return 0;
    }

    bool isFaultTermination(SweepTerminationReason reason)
    {
        return (reason != SweepTerminationReason::Completed) && (reason != SweepTerminationReason::Aborted);
    }

    bool ordinaryAbortResult(VnaErrorCode code)
    {
        return (code == VnaErrorCode::None) || (code == VnaErrorCode::Cancelled);
    }

    VnaErrorCode errorCodeForTermination(SweepTerminationReason reason)
    {
        static constexpr std::array<VnaErrorCode, 6> codes{VnaErrorCode::InvalidState,   VnaErrorCode::Cancelled,
                                                           VnaErrorCode::ConnectionLost, VnaErrorCode::Timeout,
                                                           VnaErrorCode::DataLoss,       VnaErrorCode::HardwareFault};
        return codes.at(static_cast<std::size_t>(reason));
    }

    std::size_t lostFrameCount(SweepTerminationReason reason, std::size_t expected, std::size_t received)
    {
        if ((reason != SweepTerminationReason::DataLoss) || (received >= expected)) {
            return 0;
        }
        return expected - received;
    }

}  // namespace

std::shared_ptr<VnaBufferPool> VnaSessionController::activatePreparedSweep(SweepId sweepId)
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (!prepared_ || !frameTracker_ || !pool_) {
        throw std::logic_error("prepared sweep resources are unavailable");
    }
    // Execute 已返回且旧路由已注销后 Actor 才会收到下一 Start，因此可原位复位覆盖状态。
    frameTracker_->resetForSweep();
    cancellation_.store(false);
    activeSweep_ = sweepId;
    completionEmitted_ = false;
    pendingAbortCommand_ = 0;
    sweepErrorCode_ = VnaErrorCode::None;
    return pool_;
}

void VnaSessionController::rollbackSweepStart(SweepId sweepId, bool hostRouteRegistered) noexcept
{
    // 任务交给 Worker 之前的任何异常都必须撤销全局路由和会话内租约。
    cancellation_.store(true);
    if (hostRouteRegistered) {
        HostServices::unregisterSweep(sweepId);
    }
    std::lock_guard<std::mutex> lock(dataMutex_);
    if (activeSweep_ == sweepId) {
        activeSweep_ = 0;
        completionEmitted_ = true;
        pendingAbortCommand_ = 0;
        activeConfiguration_ = 0;
        prepared_.reset();
        frameTracker_.reset();
        pool_.reset();
    }
}

void VnaSessionController::processStart(const Command &command)
{
    bool configurationActive = false;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // ConfigurationId 防止重新准备后误启动旧配置。
        configurationActive = (state_ == DeviceState::Prepared) && prepared_ && frameTracker_ && pool_ &&
                              (command.configurationId == activeConfiguration_);
    }
    // fail() 可能等待事件队列，必须在释放 dataMutex_ 后调用，避免回调重入死锁。
    if (!configurationActive) {
        fail(command.commandId, sweepError(VnaErrorCode::ConfigurationExpired, "configuration is not active"));
        return;
    }
    transition(DeviceState::Starting);
    const SweepId sweepId = reserveSweepIdentifier();
    if (sweepId == 0) {
        transition(DeviceState::Prepared);
        fail(command.commandId, sweepError(VnaErrorCode::ResourceExhausted, "sweep identifier is exhausted"));
        return;
    }
    bool hostRouteRegistered = false;
    try {
        auto pool = activatePreparedSweep(sweepId);
        // 驱动开始前必须先注册 HostApi 路由，否则第一批数据可能找不到所属缓冲池。
        hostRouteRegistered = HostServices::registerSweep(sweepId, std::move(pool), cancellation_);
        if (!hostRouteRegistered) {
            rollbackSweepStart(sweepId, false);
            transition(DeviceState::Fault);
            fail(command.commandId, sweepError(VnaErrorCode::InternalError, "sweep registration failed"));
            return;
        }
        transition(DeviceState::Running);
        VnaLogger::write(
            VnaLogLevel::Info, "扫描已启动，deviceId=" + descriptor_.deviceId + "，sweepId=" + std::to_string(sweepId));
        dispatcher_->postStartCompleted(command.commandId, sweepId);
        submitSweep(sweepId);
    } catch (...) {
        rollbackSweepStart(sweepId, hostRouteRegistered);
        throw;
    }
}

void VnaSessionController::processAbort(const Command &command)
{
    const VnaError validation = validateAbort(command);
    if (validation.code != VnaErrorCode::None) {
        fail(command.commandId, validation);
        return;
    }
    // 先设置宿主取消标志，即使驱动 RequestAbort 失败，轮询回调仍能观察到取消。
    cancellation_.store(true);
    VnaError result;
    {
        // shutdown 也可发起中止，驱动契约只要求它与 Execute 并发，不要求两个中止并发安全。
        std::lock_guard<std::mutex> lock(driverAbortMutex_);
        result = adapter_->requestAbort(command.sweepId);
    }
    if (abortRequestAwaitsSweep(result.code)) {
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            pendingAbortCommand_ = command.commandId;
        }
        // InvalidState 可能发生在 StartCompleted 与 Execute 入口之间，不能在 Worker 终态前过早失败。
        transition(DeviceState::Aborting);
        VnaLogger::write(
            VnaLogLevel::Info, "中止请求等待扫描终态，deviceId=" + descriptor_.deviceId + "，commandId=" +
                                   std::to_string(command.commandId) + "，sweepId=" + std::to_string(command.sweepId) +
                                   "，requestCode=" + std::to_string(static_cast<std::uint32_t>(result.code)));
    } else {
        // RequestAbort 自身返回 Cancelled 不是扫描主动中止成功，必须按中止路径故障收尾。
        const VnaErrorCode sweepErrorCode =
            result.code == VnaErrorCode::Cancelled ? VnaErrorCode::HardwareFault : result.code;
        recordSweepError(sweepErrorCode);
        transition(DeviceState::Fault);
        fail(command.commandId, std::move(result));
    }
}

VnaError VnaSessionController::validateAbort(const Command &command) const
{
    std::lock_guard<std::mutex> lock(dataMutex_);
    if ((state_ != DeviceState::Running) || (command.sweepId != activeSweep_)) {
        return sweepError(VnaErrorCode::InvalidState, "sweep is not running");
    }
    if (!capabilities_.supportsAbort) {
        return sweepError(VnaErrorCode::UnsupportedPlan, "device does not support abort");
    }
    return {};
}

void VnaSessionController::finishSweep(SweepId sweepId, VnaError error)
{
    std::size_t expected = 0;
    std::size_t received = 0;
    SweepTerminationReason reason;
    CommandId abortCommand = 0;
    VnaErrorCode recordedError = VnaErrorCode::None;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // 重复或过期的 Worker 完成通知不能再次释放池或发送完成事件。
        if (completionEmitted_ || (activeSweep_ != sweepId)) {
            return;
        }
        if (frameTracker_) {
            expected = frameTracker_->expectedFrameCount();
            received = frameTracker_->reservedFrameCount();
        }
        reason = normalizeSweepTermination(error.code, sweepErrorCode_, received, expected);
        abortCommand = pendingAbortCommand_;
        recordedError = sweepErrorCode_;
        completionEmitted_ = true;
        activeSweep_ = 0;
        pendingAbortCommand_ = 0;
        if (isFaultTermination(reason)) {
            // 故障可能使设备内部配置未知，因此旧 ConfigurationId 必须失效。
            activeConfiguration_ = 0;
            prepared_.reset();
            frameTracker_.reset();
            pool_.reset();
        }
    }

    // 状态和完成事件均在释放 dataMutex_ 后发布，避免用户回调反向调用时死锁。
    transition(isFaultTermination(reason) ? DeviceState::Fault : DeviceState::Prepared);
    const std::size_t lost = lostFrameCount(reason, expected, received);
    dispatcher_->postSweepCompleted(SweepCompletion{sweepId, reason, expected, received, lost});
    finishAbortCommand(abortCommand, sweepId, reason, recordedError, std::move(error));
    VnaLogger::write(
        VnaLogLevel::Info, "扫描已结束，deviceId=" + descriptor_.deviceId + "，sweepId=" + std::to_string(sweepId) +
                               "，reason=" + std::to_string(static_cast<std::uint32_t>(reason)) +
                               "，received=" + std::to_string(received) + "，expected=" + std::to_string(expected));
}

void VnaSessionController::finishAbortCommand(
    CommandId commandId, SweepId sweepId, SweepTerminationReason reason, VnaErrorCode recordedError,
    VnaError workerError)
{
    if (commandId == 0) {
        return;
    }
    if (reason == SweepTerminationReason::Aborted) {
        VnaLogger::write(
            VnaLogLevel::Info, "中止命令已完成，deviceId=" + descriptor_.deviceId +
                                   "，commandId=" + std::to_string(commandId) + "，sweepId=" + std::to_string(sweepId));
        dispatcher_->postAbortCompleted(commandId, sweepId);
        return;
    }
    // 请求虽被驱动接受，但扫描未实际进入 Aborted 时，Abort CommandId 必须得到失败终态。
    if (ordinaryAbortResult(workerError.code)) {
        workerError.code = recordedError == VnaErrorCode::None ? errorCodeForTermination(reason) : recordedError;
        workerError.description = "accepted abort did not end with an aborted sweep";
    }
    workerError.sweepId = sweepId;
    fail(commandId, std::move(workerError));
}

}  // namespace vna::detail
