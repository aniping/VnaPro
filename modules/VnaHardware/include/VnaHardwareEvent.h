#ifndef VNA_HARDWARE_VNA_HARDWARE_EVENT_H
#define VNA_HARDWARE_VNA_HARDWARE_EVENT_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "VnaError.h"
#include "VnaSweepPlan.h"
#include "VnaWaveBlock.h"

namespace vna {

// 状态只由设备 Actor 修改；Fault 需要断开或关闭会话后恢复。
enum class DeviceState : std::uint8_t {
    Disconnected,
    Connecting,
    Idle,
    Preparing,
    Prepared,
    Starting,
    Running,
    Aborting,
    Disconnecting,
    Fault
};

// 完成原因描述整次扫频的终态，不替代单样本质量标志。
enum class SweepTerminationReason : std::uint8_t {
    Completed,
    Aborted,
    ConnectionLost,
    Timeout,
    DataLoss,
    HardwareFault
};

struct StateChangedEvent {
    // previous/current 成对发布，观察者无需另行查询即可重建状态迁移。
    DeviceState previous{DeviceState::Disconnected};
    DeviceState current{DeviceState::Disconnected};
};

struct ConnectCompletedEvent {
    CommandId commandId{0};
};

struct PrepareCompletedEvent {
    CommandId commandId{0};
    // Hardware 与上层共享同一不可变快照；它不借用驱动内存，也不允许跨层修改。
    std::shared_ptr<const PreparedSweep> prepared;
};

struct StartCompletedEvent {
    CommandId commandId{0};
    SweepId sweepId{0};
};

struct AbortCompletedEvent {
    // abort 命令只在目标扫描实际进入 Aborted 终态后完成，不能把请求已接受误当成完成。
    CommandId commandId{0};
    SweepId sweepId{0};
};

struct DisconnectCompletedEvent {
    CommandId commandId{0};
};

struct SweepCompletion {
    SweepId sweepId{0};
    SweepTerminationReason reason{SweepTerminationReason::Completed};
    // expected/received 用于完整性核对；帧按“频点 × 激励”计数。
    std::size_t expectedFrameCount{0};
    std::size_t receivedFrameCount{0};
    // lostFrameCount 仅在 DataLoss 时表示确定缺失的帧数。
    std::size_t lostFrameCount{0};
};

struct CommandFailedEvent {
    CommandId commandId{0};
    VnaError error;
};

// 同一设备的回调在其事件线程上串行执行，不持有会话状态锁，回调可重入设备 API。
// 不同设备可并行回调；sink 若共享上层状态，该状态的锁由调用方持有。
class IVnaHardwareEventSink {
public:
    virtual ~IVnaHardwareEventSink();
    virtual void onStateChanged(const StateChangedEvent &event) = 0;
    virtual void onConnectCompleted(const ConnectCompletedEvent &event) = 0;
    virtual void onPrepareCompleted(const PrepareCompletedEvent &event) = 0;
    virtual void onStartCompleted(const StartCompletedEvent &event) = 0;
    virtual void onAbortCompleted(const AbortCompletedEvent &event) = 0;
    virtual void onDisconnectCompleted(const DisconnectCompletedEvent &event) = 0;
    // 波块按移动语义交付；回调取得唯一所有权，析构时自动归还池中槽位。
    virtual void onWaveBlock(VnaWaveBlock block) = 0;
    virtual void onSweepCompleted(const SweepCompletion &event) = 0;
    virtual void onCommandFailed(const CommandFailedEvent &event) = 0;
};

namespace detail {
    class VnaEventDispatcher;
}

class Subscription final {
public:
    Subscription();
    // 析构是同步退订屏障：返回后不会再开始新的 sink 回调。
    ~Subscription();
    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

private:
    friend class detail::VnaEventDispatcher;
    Subscription(std::weak_ptr<detail::VnaEventDispatcher> dispatcher, std::uint64_t id);
    void reset();

    // 弱引用避免订阅句柄反向延长整个设备会话的生命周期。
    std::weak_ptr<detail::VnaEventDispatcher> dispatcher_;
    std::uint64_t id_{0};
};

}  // namespace vna

#endif
