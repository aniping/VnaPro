#ifndef VNA_HARDWARE_VNA_EVENT_DISPATCHER_H
#define VNA_HARDWARE_VNA_EVENT_DISPATCHER_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include "VnaHardwareEvent.h"

namespace vna::detail {

// 每台设备一个分发线程；所有上层回调串行执行且不持有设备状态锁。
class VnaEventDispatcher final : public std::enable_shared_from_this<VnaEventDispatcher> {
public:
    // 必须通过工厂创建，使分发线程能够捕获 shared_ptr 保证自身生命周期。
    static std::shared_ptr<VnaEventDispatcher> create(std::size_t queueCapacity);
    ~VnaEventDispatcher();
    VnaEventDispatcher(const VnaEventDispatcher &) = delete;
    VnaEventDispatcher &operator=(const VnaEventDispatcher &) = delete;

    Subscription subscribe(IVnaHardwareEventSink &sink);
    void remove(std::uint64_t subscriptionId);
    // post* 只入队不直接回调；控制事件与数据事件在 Impl 内采用不同背压策略。
    void postStateChanged(DeviceState previous, DeviceState current);
    void postConnectCompleted(CommandId commandId);
    void postPrepareCompleted(CommandId commandId, PreparedSweep prepared);
    void postStartCompleted(CommandId commandId, SweepId sweepId);
    bool postWaveBlock(VnaWaveBlock block);
    void postSweepCompleted(SweepCompletion completion);
    void postCommandFailed(CommandId commandId, VnaError error);
    // 幂等关闭队列、线程和订阅者；允许从回调线程内触发。
    void shutdown();

private:
    class Impl;
    explicit VnaEventDispatcher(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace vna::detail

#endif
