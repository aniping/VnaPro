#ifndef VNA_HARDWARE_VNA_BUFFER_POOL_H
#define VNA_HARDWARE_VNA_BUFFER_POOL_H

#include <chrono>
#include <condition_variable>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaSweepPlan.h"
#include "VnaWaveBlock.h"

namespace vna::detail {

// 固定槽位池把 ABI 裸句柄限制在受控生命周期内，并给生产者提供明确的背压。
class VnaBufferPool final : public std::enable_shared_from_this<VnaBufferPool> {
public:
    // 回调取得移动专属数据块，并把协议错误或下游背压的准确状态返回给驱动。
    using CommitCallback = std::function<VnaStatusV1(VnaWaveBlock)>;

    static std::shared_ptr<VnaBufferPool> create(
        const std::vector<VnaWaveLayout> &layouts, std::size_t pointCapacity, std::size_t slotCount,
        CommitCallback callback);
    ~VnaBufferPool();
    VnaBufferPool(const VnaBufferPool &) = delete;
    VnaBufferPool &operator=(const VnaBufferPool &) = delete;

    VnaStatusV1 acquire(
        SweepId sweepId, LayoutId layoutId, std::uint32_t pointCapacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view);
    VnaStatusV1 commit(
        VnaBufferHandle handle, std::uint32_t firstPointIndex, std::uint32_t pointCount, std::uint64_t sequence);
    void discard(VnaBufferHandle handle);
    void release(std::size_t slotIndex);
    std::uint32_t pointCount(std::size_t slotIndex) const;
    std::uint32_t waveCount(std::size_t slotIndex) const;
    std::uint32_t firstPointIndex(std::size_t slotIndex) const;
    std::complex<double> value(std::size_t slotIndex, std::size_t waveIndex, std::size_t pointOffset) const;
    std::uint32_t quality(std::size_t slotIndex, std::size_t waveIndex, std::size_t pointOffset) const;
    void copyWaveTo(
        std::size_t slotIndex, std::size_t waveIndex, std::complex<double> *destinationValues,
        std::uint32_t *destinationQuality, std::size_t pointCount) const;

private:
    struct Slot;
    VnaBufferPool(
        std::vector<VnaWaveLayout> layouts, std::size_t pointCapacity, std::size_t slotCount, CommitCallback callback);
    bool validCommit(const Slot *slot, std::uint32_t pointCount) const;
    Slot *resolveHandle(VnaBufferHandle handle);
    const Slot &checkedSlot(std::size_t slotIndex) const;
    std::size_t checkedValueIndex(const Slot &slot, std::size_t waveIndex, std::size_t pointOffset) const;

    // layouts_ 决定每个 layoutId 的 waveCount；pointCapacity_ 是池的最大容量。
    // 每次 Acquire 记录的 acquiredCapacity 才是该块各波之间的实际跨距。
    std::vector<VnaWaveLayout> layouts_;
    std::size_t pointCapacity_{0};
    std::vector<std::unique_ptr<Slot>> slots_;
    // callback_ 在不持 mutex_ 时调用；其余槽位状态均由 mutex_ 与 available_ 协调。
    CommitCallback callback_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
};

}  // namespace vna::detail

#endif
