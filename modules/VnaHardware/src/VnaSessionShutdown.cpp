#include "VnaSessionController.h"

#include <chrono>

namespace vna::detail {

void VnaSessionController::shutdown()
{
    // 析构与显式关闭可能同时到达，call_once 保证完整关闭序列只执行一次。
    std::call_once(shutdownOnce_, [this] { performShutdown(); });
}

void VnaSessionController::performShutdown()
{
    // 先关闭分发器解除数据背压，否则 Worker 可能等待队列容量而无法响应取消。
    dispatcher_->shutdown();
    cancellation_.store(true);

    SweepId sweepId = 0;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        sweepId = activeSweep_;
    }
    if (sweepId != 0) {
        // RequestAbort 是加速路径；HostApi 取消标志才是始终存在的兜底路径。
        adapter_->requestAbort(sweepId);
        std::unique_lock<std::mutex> lock(dataMutex_);
        sweepFinished_.wait_for(lock, std::chrono::seconds(5), [this] { return activeSweep_ == 0; });
    }

    {
        // Worker 可能仍在 Execute 中，先发停止信号再 join，保证设备句柄不会提前析构。
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerStopping_ = true;
    }
    workerReady_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    {
        // Worker 已结束后不再产生完成命令，此时清空 Actor 队列是安全的。
        std::lock_guard<std::mutex> lock(commandMutex_);
        actorStopping_ = true;
        commands_.clear();
    }
    commandReady_.notify_all();
    if (actorThread_.joinable()) {
        actorThread_.join();
    }

    // 两条内部线程均已退出，最后断开设备，避免并发访问驱动句柄。
    adapter_->disconnect();
}

}  // namespace vna::detail
