#include "VnaSessionController.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "HostServices.h"
#include "VnaBufferPool.h"

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

    SweepTerminationReason terminationReason(VnaErrorCode code)
    {
        using Mapping = std::pair<VnaErrorCode, SweepTerminationReason>;
        // 只列需要保留独立终态语义的错误，其余错误统一归为 HardwareFault。
        static constexpr std::array<Mapping, 5> mappings{
            Mapping{VnaErrorCode::None, SweepTerminationReason::Completed},
            Mapping{VnaErrorCode::Cancelled, SweepTerminationReason::Aborted},
            Mapping{VnaErrorCode::ConnectionLost, SweepTerminationReason::ConnectionLost},
            Mapping{VnaErrorCode::Timeout, SweepTerminationReason::Timeout},
            Mapping{VnaErrorCode::DataLoss, SweepTerminationReason::DataLoss}};
        const auto match =
            std::find_if(mappings.begin(), mappings.end(), [code](const auto &item) { return item.first == code; });
        return match == mappings.end() ? SweepTerminationReason::HardwareFault : match->second;
    }

    bool isFaultTermination(SweepTerminationReason reason)
    {
        return (reason != SweepTerminationReason::Completed) && (reason != SweepTerminationReason::Aborted);
    }

    SweepTerminationReason normalizedTermination(
        VnaErrorCode code, bool abortAccepted, bool sweepFaulted, std::size_t received, std::size_t expected)
    {
        SweepTerminationReason reason = terminationReason(code);
        // 已接受的中止优先于驱动随后返回的普通 Cancelled/OK。
        if (abortAccepted) {
            reason = SweepTerminationReason::Aborted;
        }
        // 数据面已报告故障时，不能被 Execute 的表面成功覆盖。
        if (sweepFaulted) {
            reason = SweepTerminationReason::HardwareFault;
        }
        // Execute 返回 OK 但帧数不足仍属于数据丢失，不能报告成功。
        if ((reason == SweepTerminationReason::Completed) && (received != expected)) {
            reason = SweepTerminationReason::DataLoss;
        }
        return reason;
    }

    std::size_t lostFrameCount(SweepTerminationReason reason, std::size_t expected, std::size_t received)
    {
        if ((reason != SweepTerminationReason::DataLoss) || (received >= expected)) {
            return 0;
        }
        return expected - received;
    }

    bool validBatchCapacity(std::size_t batchPoints)
    {
        // ABI 使用 uint32_t 表示容量，宿主 size_t 必须先验证可无损转换。
        return (batchPoints > 0) && (batchPoints <= std::numeric_limits<std::uint32_t>::max());
    }

}  // namespace

void VnaSessionController::processStart(const Command &command)
{
    std::vector<VnaWaveLayout> layouts;
    std::size_t batchPoints = 0;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // ConfigurationId 防止重新准备后误启动旧配置。
        if ((state_ != DeviceState::Prepared) || (command.configurationId != activeConfiguration_)) {
            fail(command.commandId, sweepError(VnaErrorCode::ConfigurationExpired, "configuration is not active"));
            return;
        }
        layouts = prepared_.layouts;
        batchPoints = prepared_.report.preferredBatchPoints;
    }
    if (!validBatchCapacity(batchPoints)) {
        fail(command.commandId, sweepError(VnaErrorCode::ProtocolError, "driver returned invalid batch capacity"));
        return;
    }

    transition(DeviceState::Starting);
    const SweepId sweepId = globalSweepIdentifier.fetch_add(1);
    // 固定八槽池提供有界内存与背压；回调只负责把只读块交给事件线程。
    auto pool = VnaBufferPool::create(
        layouts, batchPoints, 8, [this](VnaWaveBlock block) { return acceptBlock(std::move(block)); });
    cancellation_.store(false);
    receivedFrames_.store(0);
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        activeSweep_ = sweepId;
        completionEmitted_ = false;
        abortAccepted_ = false;
        sweepFaulted_ = false;
        pool_ = pool;
    }

    // 驱动开始前必须先注册 HostApi 路由，否则第一批数据可能找不到所属缓冲池。
    if (!HostServices::registerSweep(sweepId, std::move(pool), cancellation_)) {
        finishSweep(sweepId, sweepError(VnaErrorCode::InternalError, "duplicate sweep identifier"));
        fail(command.commandId, sweepError(VnaErrorCode::InternalError, "sweep registration failed"));
        return;
    }
    transition(DeviceState::Running);
    dispatcher_->postStartCompleted(command.commandId, sweepId);
    submitSweep(sweepId);
}

void VnaSessionController::processAbort(const Command &command)
{
    const VnaError validation = validateAbort(command);
    if (validation.code != VnaErrorCode::None) {
        fail(command.commandId, validation);
        return;
    }
    transition(DeviceState::Aborting);
    // 先设置宿主取消标志，即使驱动 RequestAbort 失败，轮询回调仍能观察到取消。
    cancellation_.store(true);
    VnaError result = adapter_->requestAbort(command.sweepId);
    if (result.code == VnaErrorCode::None) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        abortAccepted_ = true;
    } else if (result.code == VnaErrorCode::InvalidState) {
        // 扫频可能恰好自然结束；由 WorkerFinished 决定最终状态。
        fail(command.commandId, std::move(result));
    } else {
        {
            std::lock_guard<std::mutex> lock(dataMutex_);
            sweepFaulted_ = true;
        }
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
    const std::size_t expected = expectedFrames();
    const std::size_t received = receivedFrames_.load();
    SweepTerminationReason reason;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // 重复或过期的 Worker 完成通知不能再次释放池或发送完成事件。
        if (completionEmitted_ || (activeSweep_ != sweepId)) {
            return;
        }
        reason = normalizedTermination(error.code, abortAccepted_, sweepFaulted_, received, expected);
        completionEmitted_ = true;
        activeSweep_ = 0;
        pool_.reset();
        if (isFaultTermination(reason)) {
            // 故障可能使设备内部配置未知，因此旧 ConfigurationId 必须失效。
            activeConfiguration_ = 0;
            prepared_ = {};
        }
    }

    // 状态和完成事件均在释放 dataMutex_ 后发布，避免用户回调反向调用时死锁。
    transition(isFaultTermination(reason) ? DeviceState::Fault : DeviceState::Prepared);
    const std::size_t lost = lostFrameCount(reason, expected, received);
    dispatcher_->postSweepCompleted(SweepCompletion{sweepId, reason, expected, received, lost});
    sweepFinished_.notify_all();
}

}  // namespace vna::detail
