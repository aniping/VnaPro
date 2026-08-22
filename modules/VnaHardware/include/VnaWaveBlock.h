#ifndef VNA_HARDWARE_VNA_WAVE_BLOCK_H
#define VNA_HARDWARE_VNA_WAVE_BLOCK_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "VnaError.h"

namespace vna {

namespace detail {
    class VnaBufferPool;
}

// 只读、仅移动的波块视图；对象存活期间对应缓冲池槽位不会被复用。
class VnaWaveBlock final {
public:
    VnaWaveBlock();
    ~VnaWaveBlock();
    VnaWaveBlock(VnaWaveBlock &&other) noexcept;
    VnaWaveBlock &operator=(VnaWaveBlock &&other) noexcept;
    VnaWaveBlock(const VnaWaveBlock &) = delete;
    VnaWaveBlock &operator=(const VnaWaveBlock &) = delete;

    // sweepId/sequence 用于检测跨扫频混入、重复块和乱序块。
    SweepId sweepId() const;
    std::uint64_t sequence() const;
    LayoutId layoutId() const;
    std::uint32_t firstPointIndex() const;
    // pointCount 是本块实际点数，可能小于驱动 Acquire 时请求的容量。
    std::uint32_t pointCount() const;
    std::uint32_t waveCount() const;
    // 索引越界或底层槽位已释放时抛出标准异常。
    std::complex<double> value(std::size_t waveIndex, std::size_t pointOffset) const;
    // quality 返回 WaveQuality 位集合；Valid 的数值为零。
    std::uint32_t quality(std::size_t waveIndex, std::size_t pointOffset) const;

private:
    friend class detail::VnaBufferPool;
    VnaWaveBlock(
        std::shared_ptr<detail::VnaBufferPool> pool, std::size_t slotIndex, SweepId sweepId, std::uint64_t sequence,
        LayoutId layoutId);
    void release();

    std::shared_ptr<detail::VnaBufferPool> pool_;
    std::size_t slotIndex_{0};
    SweepId sweepId_{0};
    std::uint64_t sequence_{0};
    LayoutId layoutId_{0};
};

}  // namespace vna

#endif
