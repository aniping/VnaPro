#include "VnaSessionController.h"

#include <mutex>

#include "HostServices.h"

namespace vna::detail {

void VnaSessionController::shutdown()
{
    // 析构与显式关闭可能同时到达，call_once 保证完整关闭序列只执行一次。
    std::call_once(shutdownOnce_, [this] { performShutdown(); });
}

void VnaSessionController::performShutdown()
{
    stopActorForShutdown();
    const SweepId sweepId = requestShutdownAbort();
    stopWorkerForShutdown(sweepId);
    clearShutdownResources();
    // 两条内部线程均已退出，最后断开设备，避免并发访问驱动句柄。
    adapter_->disconnect();
}

void VnaSessionController::stopActorForShutdown()
{
    // 先关闭事件域并停止 Actor；join 后不会再有命令启动新扫描或清除取消位。
    dispatcher_->shutdown();
    cancellation_.store(true);
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        actorStopping_ = true;
        for (auto &slot : commandSlots_) {
            slot.reset();
        }
        queuedExternalCommands_ = 0;
        workerCompletion_.reset();
        // Actor 可能正在锁外处理一条已出队命令；计数保留到该处理返回，避免释放时下溢。
    }
    commandReady_.notify_all();
    if (actorThread_.joinable()) {
        actorThread_.join();
    }
}

SweepId VnaSessionController::requestShutdownAbort()
{
    SweepId sweepId = 0;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        sweepId = activeSweep_;
    }
    if (sweepId != 0) {
        // Actor 可能在停机屏障前刚启动扫描，再次置位保证其 initializeSweep 不能覆盖取消。
        cancellation_.store(true);
        {
            std::lock_guard<std::mutex> lock(driverAbortMutex_);
            adapter_->requestAbort(sweepId);
        }
    }
    return sweepId;
}

void VnaSessionController::stopWorkerForShutdown(SweepId sweepId)
{
    {
        // Worker 可能仍在 Execute 中，先发停止信号再 join，保证设备句柄不会提前析构。
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerStopping_ = true;
    }
    workerReady_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    // Worker 正常返回会先注销；这里再做一次幂等兜底，覆盖任务交付前的异常路径。
    if (sweepId != 0) {
        HostServices::unregisterSweep(sweepId);
    }
}

void VnaSessionController::clearShutdownResources()
{
    {
        // 事件域已关闭且 Actor 已退出，Worker 完成后直接回收不再需要发布终态的扫描资源。
        std::lock_guard<std::mutex> lock(dataMutex_);
        activeSweep_ = 0;
        activeConfiguration_ = 0;
        completionEmitted_ = true;
        pendingAbortCommand_ = 0;
        prepared_.reset();
        capabilities_ = {};
        frameTracker_.reset();
        pool_.reset();
        state_ = DeviceState::Disconnected;
    }
}

}  // namespace vna::detail
