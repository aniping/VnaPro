#ifndef VNA_TESTS_VNA_INTEGRATION_TEST_SUPPORT_H
#define VNA_TESTS_VNA_INTEGRATION_TEST_SUPPORT_H

#include <chrono>
#include <complex>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "VnaHardwareManager.h"

namespace vna::test {

// 收集一次异步发现的终态；测试线程只通过 wait() 观察回调线程写入的数据。
class DiscoverySink final : public IVnaDiscoverySink {
public:
    void onDeviceFound(DiscoveryId discoveryId, const DeviceDescriptor &descriptor) override;
    void onDiscoveryCompleted(DiscoveryId discoveryId) override;
    void onDiscoveryFailed(DiscoveryId discoveryId, const VnaError &error) override;
    bool wait();
    DeviceDescriptor firstDevice() const;
    bool succeeded() const;

private:
    // 回调线程写、测试线程读，以下所有字段都由 mutex_ 保护。
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<DeviceDescriptor> devices_;
    VnaError error_;
    bool completed_{false};
};

// 保存设备事件的只读测试快照，覆盖状态、配置、波块、扫频终态和命令失败。
class HardwareSink final : public IVnaHardwareEventSink {
public:
    void onStateChanged(const StateChangedEvent &event) override;
    void onConnectCompleted(const ConnectCompletedEvent &event) override;
    void onPrepareCompleted(const PrepareCompletedEvent &event) override;
    void onStartCompleted(const StartCompletedEvent &event) override;
    void onWaveBlock(VnaWaveBlock block) override;
    void onSweepCompleted(const SweepCompletion &event) override;
    void onCommandFailed(const CommandFailedEvent &event) override;

    bool waitConnected();
    bool waitPrepared(std::size_t count);
    bool waitStarted(std::size_t count);
    bool waitCompleted(std::size_t count);
    bool waitFailures(std::size_t count);
    bool waitState(DeviceState state);
    ConfigurationId configuration() const;
    SweepId sweep(std::size_t index) const;
    SweepCompletion completion(std::size_t index) const;
    std::size_t blockCount() const;
    std::uint64_t blockSequence(std::size_t index) const;
    std::uint32_t blockPointCount(std::size_t index) const;
    std::complex<double> blockValue(std::size_t block, std::size_t wave, std::size_t point) const;
    void clearBlocks();
    std::size_t failureCount() const;

private:
    template <typename Predicate>
    bool waitFor(Predicate predicate)
    {
        // predicate 在持锁状态下执行，与各回调更新使用同一互斥量，不会漏掉通知。
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), predicate);
    }

    // blocks_ 保存移动专属租约，其存活时间也用于构造缓冲池背压测试。
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    DeviceState state_{DeviceState::Disconnected};
    bool connected_{false};
    ConfigurationId configuration_{0};
    std::size_t prepareCount_{0};
    std::vector<SweepId> sweeps_;
    std::vector<VnaWaveBlock> blocks_;
    std::vector<SweepCompletion> completions_;
    std::vector<VnaError> failures_;
};

// 构造跨五个频点的确定性计划，用于验证批次边界和复数波模型。
VnaSweepPlan makePlan();
void verifyCompletedSweep(const HardwareSink &events);

}  // namespace vna::test

#endif
