#include "VnaWaveBlock.h"

#include <stdexcept>
#include <utility>

#include "VnaBufferPool.h"

namespace vna {

VnaWaveBlock::VnaWaveBlock() = default;
VnaWaveBlock::~VnaWaveBlock()
{
    // 数据块也是缓冲槽租约；析构即归还，调用者无需手工 release。
    release();
}

VnaWaveBlock::VnaWaveBlock(
    std::shared_ptr<detail::VnaBufferPool> pool, std::size_t slotIndex, SweepId sweepId, std::uint64_t sequence,
    LayoutId layoutId)
    : pool_(std::move(pool)), slotIndex_(slotIndex), sweepId_(sweepId), sequence_(sequence), layoutId_(layoutId)
{
}

VnaWaveBlock::VnaWaveBlock(VnaWaveBlock &&other) noexcept
    : pool_(std::move(other.pool_)),
      slotIndex_(other.slotIndex_),
      sweepId_(other.sweepId_),
      sequence_(other.sequence_),
      layoutId_(other.layoutId_)
{
    // pool_ 被移走后源对象成为空块，其析构不会重复归还同一槽位。
}

VnaWaveBlock &VnaWaveBlock::operator=(VnaWaveBlock &&other) noexcept
{
    if (this != &other) {
        // 覆盖前先归还当前租约，再接管来源租约，保持每个槽位恰好一个所有者。
        release();
        pool_ = std::move(other.pool_);
        slotIndex_ = other.slotIndex_;
        sweepId_ = other.sweepId_;
        sequence_ = other.sequence_;
        layoutId_ = other.layoutId_;
    }
    return *this;
}

SweepId VnaWaveBlock::sweepId() const
{
    return sweepId_;
}
std::uint64_t VnaWaveBlock::sequence() const
{
    return sequence_;
}
LayoutId VnaWaveBlock::layoutId() const
{
    return layoutId_;
}

std::uint32_t VnaWaveBlock::firstPointIndex() const
{
    // 空块是合法的 moved-from 状态，元数据访问返回零而不是解引用空池。
    return pool_ ? pool_->firstPointIndex(slotIndex_) : 0;
}

std::uint32_t VnaWaveBlock::pointCount() const
{
    return pool_ ? pool_->pointCount(slotIndex_) : 0;
}

std::uint32_t VnaWaveBlock::waveCount() const
{
    return pool_ ? pool_->waveCount(slotIndex_) : 0;
}

std::complex<double> VnaWaveBlock::value(std::size_t waveIndex, std::size_t pointOffset) const
{
    // 值访问对空块抛出逻辑错误；索引边界由池按已提交块的实际尺寸校验。
    if (!pool_) {
        throw std::logic_error("Wave block has no storage");
    }
    return pool_->value(slotIndex_, waveIndex, pointOffset);
}

std::uint32_t VnaWaveBlock::quality(std::size_t waveIndex, std::size_t pointOffset) const
{
    if (!pool_) {
        throw std::logic_error("Wave block has no storage");
    }
    return pool_->quality(slotIndex_, waveIndex, pointOffset);
}

void VnaWaveBlock::copyWaveTo(
    std::size_t waveIndex, std::complex<double> *destinationValues, std::uint32_t *destinationQuality,
    std::size_t pointCount) const
{
    if (!pool_) {
        throw std::logic_error("Wave block has no storage");
    }
    pool_->copyWaveTo(slotIndex_, waveIndex, destinationValues, destinationQuality, pointCount);
}

void VnaWaveBlock::release()
{
    if (pool_) {
        // 先移走 shared_ptr，再调用 pool，防止 release 路径重入时重复观察到租约。
        auto owner = std::move(pool_);
        owner->release(slotIndex_);
    }
}

}  // namespace vna
