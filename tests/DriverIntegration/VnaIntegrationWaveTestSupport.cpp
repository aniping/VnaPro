#include "VnaIntegrationTestSupport.h"

#include <utility>

namespace vna::test {

void HardwareSink::onWaveBlock(VnaWaveBlock block)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++blockCount_;
    // 常规用例持有移动租约以验证池背压；吞吐用例只计数并让形参就地归还槽位。
    if (retainBlocks_) {
        blocks_.push_back(std::move(block));
    }
    changed_.notify_all();
}

bool HardwareSink::waitBlocks(std::size_t count)
{
    return waitFor([this, count] { return blockCount_ >= count; });
}

std::size_t HardwareSink::blockCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blockCount_;
}

std::uint64_t HardwareSink::blockSequence(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(index).sequence();
}

std::uint32_t HardwareSink::blockPointCount(std::size_t index) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(index).pointCount();
}

std::complex<double> HardwareSink::blockValue(std::size_t block, std::size_t wave, std::size_t point) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(block).value(wave, point);
}

std::uint32_t HardwareSink::blockQuality(std::size_t block, std::size_t wave, std::size_t point) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return blocks_.at(block).quality(wave, point);
}

void HardwareSink::clearBlocks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // clear 触发 VnaWaveBlock 析构，归还测试刻意持有的所有缓冲槽。
    blocks_.clear();
    blockCount_ = 0;
}

void HardwareSink::discardFutureBlocks()
{
    std::lock_guard<std::mutex> lock(mutex_);
    blocks_.clear();
    blockCount_ = 0;
    retainBlocks_ = false;
}

}  // namespace vna::test
