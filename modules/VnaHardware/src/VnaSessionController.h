#ifndef VNA_HARDWARE_VNA_SESSION_CONTROLLER_H
#define VNA_HARDWARE_VNA_SESSION_CONTROLLER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "DriverModuleAdapter.h"
#include "VnaEventDispatcher.h"

namespace vna::detail {

class VnaBufferPool;
class VnaFrameTracker;

// Actor 串行状态命令，Worker 独占阻塞式 Execute；两者只用内部命令传递完成结果。
class VnaSessionController final {
public:
    VnaSessionController(DeviceDescriptor descriptor, std::unique_ptr<DriverModuleAdapter> adapter);
    ~VnaSessionController();
    VnaSessionController(const VnaSessionController &) = delete;
    VnaSessionController &operator=(const VnaSessionController &) = delete;

    DeviceDescriptor descriptor() const;
    VnaCapabilities capabilities() const;
    DeviceState state() const;
    ValidationReport validate(const VnaSweepPlan &plan) const;
    CommandId connectAsync();
    CommandId prepareAsync(VnaSweepPlan plan);
    CommandId startAsync(ConfigurationId configurationId);
    CommandId abortAsync(SweepId sweepId);
    CommandId disconnectAsync();
    Subscription subscribe(IVnaHardwareEventSink &sink);
    void shutdown();

private:
    enum class CommandKind : std::uint8_t { Connect, Prepare, Start, Abort, Disconnect, WorkerFinished };

    // 一个值类型同时承载外部命令和 Worker 完成消息；未被 kind 使用的字段保持零/空值。
    struct Command {
        CommandKind kind{CommandKind::Connect};
        CommandId commandId{0};
        ConfigurationId configurationId{0};
        SweepId sweepId{0};
        VnaSweepPlan plan;
        VnaError workerError;
    };

    CommandId nextCommandId();
    void enqueue(Command command);
    void actorLoop();
    void workerLoop();
    void process(Command command);
    void processConnect(const Command &command);
    void processPrepare(Command command);
    void processStart(const Command &command);
    void processAbort(const Command &command);
    void processDisconnect(const Command &command);
    void processWorkerFinished(const Command &command);
    VnaError validateAbort(const Command &command) const;
    std::shared_ptr<VnaBufferPool> initializeSweep(
        SweepId sweepId, VnaSweepPlan effectivePlan, std::vector<VnaWaveLayout> layouts, std::size_t batchPoints);
    void rollbackSweepStart(SweepId sweepId, bool hostRouteRegistered) noexcept;
    void submitSweep(SweepId sweepId);
    VnaStatusV1 acceptBlock(VnaWaveBlock block);
    bool recordSweepError(VnaErrorCode code);
    void transition(DeviceState next);
    void fail(CommandId commandId, VnaError error);
    void finishSweep(SweepId sweepId, VnaError error);
    void finishAbortCommand(
        CommandId commandId, SweepId sweepId, SweepTerminationReason reason, VnaErrorCode recordedError,
        VnaError workerError);
    void performShutdown();

    // descriptor_/adapter_ 在会话期不变；dispatcher_ 是所有公开回调的唯一出口。
    DeviceDescriptor descriptor_;
    std::unique_ptr<DriverModuleAdapter> adapter_;
    std::shared_ptr<VnaEventDispatcher> dispatcher_;
    // dataMutex_ 保护下面的状态快照和活动扫描身份，不保护驱动阻塞调用。
    mutable std::mutex dataMutex_;
    DeviceState state_{DeviceState::Disconnected};
    VnaCapabilities capabilities_;
    PreparedSweep prepared_;
    // configurationId 标识当前 Prepare 结果，sweepId 标识该配置的一次具体执行。
    ConfigurationId activeConfiguration_{0};
    SweepId activeSweep_{0};
    // 完成标志防止 Worker 的重复通知；中止 CommandId 要等扫描真正进入终态后才完成。
    bool completionEmitted_{false};
    CommandId pendingAbortCommand_{0};
    // 数据通路或 RequestAbort 的首个错误在 Worker 只返回 OK/Cancelled 时决定最终故障。
    VnaErrorCode sweepErrorCode_{VnaErrorCode::None};
    // cancellation_ 被 HostServices 跨线程读取；帧追踪器按布局验证覆盖且拒绝重复提交。
    std::atomic<bool> cancellation_{false};
    std::shared_ptr<VnaFrameTracker> frameTracker_;
    // pool_ 同时被 HostServices 和已发布的 VnaWaveBlock 共享，允许块晚于扫描完成释放。
    std::shared_ptr<VnaBufferPool> pool_;
    // shutdown 可从调用线程发起中止；与 Actor 的 abortAsync 共用此锁串行访问驱动。
    std::mutex driverAbortMutex_;

    // 命令队列只由 Actor 消费，避免状态转换在调用线程并发发生。
    std::mutex commandMutex_;
    std::condition_variable commandReady_;
    std::deque<Command> commands_;
    bool actorStopping_{false};
    std::thread actorThread_;

    // 驱动 Execute 可能长期阻塞，因此不能占用 Actor 线程。
    std::mutex workerMutex_;
    std::condition_variable workerReady_;
    SweepId workerSweep_{0};
    bool workerHasTask_{false};
    bool workerStopping_{false};
    std::thread workerThread_;
    // 命令 ID 可由任意调用线程申请；配置 ID 只由 actor 修改，因此无需原子类型。
    std::atomic<CommandId> nextCommand_{1};
    ConfigurationId nextConfiguration_{1};
    // shutdownOnce_ 把显式关闭、析构和 manager 关闭收敛为一次有序停机。
    std::once_flag shutdownOnce_;
};

}  // namespace vna::detail

#endif
