#include "VnaBufferPool.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace vna::detail {
namespace {

    VnaStatusV1 makeStatus(std::uint32_t code, const char *message)
    {
        VnaStatusV1 status{};
        status.code = code;
        status.message.data = message;
        status.message.size = std::char_traits<char>::length(message);
        return status;
    }

    bool validConfiguration(
        const std::vector<VnaWaveLayout> &layouts, std::size_t pointCapacity, std::size_t slotCount,
        const VnaBufferPool::CommitCallback &callback)
    {
        const std::array<bool, 4> requirements{
            !layouts.empty(), pointCapacity > 0, slotCount > 0, static_cast<bool>(callback)};
        return std::all_of(requirements.begin(), requirements.end(), [](bool valid) { return valid; });
    }

    std::size_t maximumWaveCount(const std::vector<VnaWaveLayout> &layouts)
    {
        std::size_t result = 0;
        for (const auto &layout : layouts) {
            result = std::max(result, layout.waveKeys.size());
        }
        return result;
    }

    bool representableAllocation(std::size_t waveCount, std::size_t pointCapacity)
    {
        return (waveCount > 0) && (pointCapacity <= (std::numeric_limits<std::size_t>::max() / waveCount));
    }

    bool validAcquireRequest(
        std::uint32_t requestedCapacity, std::size_t maximumCapacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *view)
    {
        const bool validOutput = (handle != nullptr) && (view != nullptr);
        return validOutput && (requestedCapacity > 0) && (requestedCapacity <= maximumCapacity);
    }

}  // namespace

struct VnaBufferPool::Slot {
    // 槽位状态机只有 Free -> Acquired -> Committed -> Free，跨状态操作一律拒绝。
    enum class State : std::uint8_t { Free, Acquired, Committed };
    VnaBufferPool *owner{nullptr};
    std::size_t index{0};
    State state{State::Free};
    SweepId sweepId{0};
    LayoutId layoutId{0};
    std::uint32_t acquiredCapacity{0};
    std::uint32_t firstPointIndex{0};
    std::uint32_t pointCount{0};
    std::uint32_t waveCount{0};
    std::vector<VnaComplex64V1> values;
    std::vector<std::uint32_t> qualityFlags;
};

std::shared_ptr<VnaBufferPool> VnaBufferPool::create(
    const std::vector<VnaWaveLayout> &layouts, std::size_t pointCapacity, std::size_t slotCount,
    CommitCallback callback)
{
    return std::shared_ptr<VnaBufferPool>(new VnaBufferPool(layouts, pointCapacity, slotCount, std::move(callback)));
}

VnaBufferPool::VnaBufferPool(
    std::vector<VnaWaveLayout> layouts, std::size_t pointCapacity, std::size_t slotCount, CommitCallback callback)
    : layouts_(std::move(layouts)), pointCapacity_(pointCapacity), callback_(std::move(callback))
{
    if (!validConfiguration(layouts_, pointCapacity_, slotCount, callback_)) {
        throw std::invalid_argument("Wave buffer pool configuration is invalid");
    }
    const std::size_t maximumWaves = maximumWaveCount(layouts_);
    if (!representableAllocation(maximumWaves, pointCapacity_)) {
        throw std::length_error("Wave buffer pool size is not representable");
    }
    // 每个槽位按最大布局一次性预分配，采集热路径只复用内存，不发生扩容。
    const std::size_t valueCount = maximumWaves * pointCapacity_;
    slots_.reserve(slotCount);
    for (std::size_t index = 0; index < slotCount; ++index) {
        auto slot = std::make_unique<Slot>();
        slot->owner = this;
        slot->index = index;
        slot->values.resize(valueCount);
        slot->qualityFlags.resize(valueCount);
        slots_.push_back(std::move(slot));
    }
}

VnaBufferPool::~VnaBufferPool() = default;

VnaStatusV1 VnaBufferPool::acquire(
    SweepId sweepId, LayoutId layoutId, std::uint32_t pointCapacity, VnaBufferHandle *handle,
    VnaMutableWaveBlockViewV1 *view)
{
    // 先验证输出地址和容量，失败时绝不触碰驱动传入的指针。
    if (!validAcquireRequest(pointCapacity, pointCapacity_, handle, view)) {
        return makeStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid wave block request");
    }
    const auto layout = std::find_if(
        layouts_.begin(), layouts_.end(), [layoutId](const auto &item) { return item.layoutId == layoutId; });
    if (layout == layouts_.end()) {
        return makeStatus(VNA_STATUS_INVALID_ARGUMENT, "unknown wave layout");
    }
    std::unique_lock<std::mutex> lock(mutex_);
    // 有界等待把消费者反压传回驱动，避免慢订阅者导致无限堆积。
    const bool available = available_.wait_for(lock, std::chrono::milliseconds(250), [this] {
        return std::any_of(
            slots_.begin(), slots_.end(), [](const auto &slot) { return slot->state == Slot::State::Free; });
    });
    if (!available) {
        return makeStatus(VNA_STATUS_TIMEOUT, "wave buffer pool exhausted");
    }
    // 条件变量谓词与持锁后的查找使用同一互斥量，因此这里一定能找到空闲槽位。
    const auto slot =
        std::find_if(slots_.begin(), slots_.end(), [](const auto &item) { return item->state == Slot::State::Free; });
    Slot &selected = **slot;
    selected.state = Slot::State::Acquired;
    selected.sweepId = sweepId;
    selected.layoutId = layoutId;
    selected.acquiredCapacity = pointCapacity;
    selected.waveCount = static_cast<std::uint32_t>(layout->waveKeys.size());
    const std::size_t activeValues = layout->waveKeys.size() * pointCapacity;
    // 默认标为缺失值；驱动只需覆盖真实采集到的样本和质量标志。
    std::fill_n(selected.values.begin(), activeValues, VnaComplex64V1{});
    std::fill_n(selected.qualityFlags.begin(), activeValues, VNA_QUALITY_MISSING);
    *handle = &selected;
    *view = VnaMutableWaveBlockViewV1{
        static_cast<std::uint32_t>(sizeof(*view)),
        pointCapacity,
        selected.waveCount,
        0,
        selected.values.data(),
        selected.qualityFlags.data()};
    return makeStatus(VNA_STATUS_OK, "");
}

VnaStatusV1 VnaBufferPool::commit(
    VnaBufferHandle handle, std::uint32_t firstPointIndex, std::uint32_t pointCount, std::uint64_t sequence)
{
    std::unique_lock<std::mutex> lock(mutex_);
    Slot *slot = resolveHandle(handle);
    if (!validCommit(slot, pointCount)) {
        return makeStatus(VNA_STATUS_INVALID_ARGUMENT, "invalid wave block commit");
    }
    slot->firstPointIndex = firstPointIndex;
    slot->pointCount = pointCount;
    slot->state = Slot::State::Committed;
    // VnaWaveBlock 接管槽位租约；最后一个块对象析构时才会把槽位归还池中。
    VnaWaveBlock block(shared_from_this(), slot->index, slot->sweepId, sequence, slot->layoutId);
    // 回调可能重入订阅/关闭路径，必须在调用前释放池互斥量。
    lock.unlock();
    if (!callback_(std::move(block))) {
        return makeStatus(VNA_STATUS_DATA_LOSS, "wave dispatch queue is full");
    }
    return makeStatus(VNA_STATUS_OK, "");
}

bool VnaBufferPool::validCommit(const Slot *slot, std::uint32_t pointCount) const
{
    if (slot == nullptr) {
        return false;
    }
    return (slot->state == Slot::State::Acquired) && (pointCount > 0) && (pointCount <= slot->acquiredCapacity);
}

void VnaBufferPool::discard(VnaBufferHandle handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Slot *slot = resolveHandle(handle);
    // discard 只撤销尚未发布的租约，已提交槽位必须由 VnaWaveBlock 生命周期释放。
    if ((slot != nullptr) && (slot->state == Slot::State::Acquired)) {
        slot->state = Slot::State::Free;
        available_.notify_one();
    }
}

void VnaBufferPool::release(std::size_t slotIndex)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Slot &slot = *slots_.at(slotIndex);
    if (slot.state == Slot::State::Committed) {
        slot.state = Slot::State::Free;
        available_.notify_one();
    }
}

std::uint32_t VnaBufferPool::pointCount(std::size_t slotIndex) const
{
    return checkedSlot(slotIndex).pointCount;
}

std::uint32_t VnaBufferPool::waveCount(std::size_t slotIndex) const
{
    return checkedSlot(slotIndex).waveCount;
}

std::uint32_t VnaBufferPool::firstPointIndex(std::size_t slotIndex) const
{
    return checkedSlot(slotIndex).firstPointIndex;
}

std::complex<double> VnaBufferPool::value(std::size_t slotIndex, std::size_t waveIndex, std::size_t pointOffset) const
{
    const Slot &slot = checkedSlot(slotIndex);
    const VnaComplex64V1 &value = slot.values.at(checkedValueIndex(slot, waveIndex, pointOffset));
    return {value.real, value.imag};
}

std::uint32_t VnaBufferPool::quality(std::size_t slotIndex, std::size_t waveIndex, std::size_t pointOffset) const
{
    const Slot &slot = checkedSlot(slotIndex);
    return slot.qualityFlags.at(checkedValueIndex(slot, waveIndex, pointOffset));
}

VnaBufferPool::Slot *VnaBufferPool::resolveHandle(VnaBufferHandle handle)
{
    // 外部 handle 可能伪造，只比较稳定地址，确认属于本池之前绝不解引用。
    const auto match = std::find_if(
        slots_.begin(), slots_.end(), [handle](const auto &slot) { return static_cast<void *>(slot.get()) == handle; });
    return match == slots_.end() ? nullptr : match->get();
}

const VnaBufferPool::Slot &VnaBufferPool::checkedSlot(std::size_t slotIndex) const
{
    const Slot &slot = *slots_.at(slotIndex);
    if (slot.state != Slot::State::Committed) {
        throw std::logic_error("Wave block storage is no longer committed");
    }
    return slot;
}

std::size_t VnaBufferPool::checkedValueIndex(const Slot &slot, std::size_t waveIndex, std::size_t pointOffset) const
{
    if ((waveIndex >= slot.waveCount) || (pointOffset >= slot.pointCount)) {
        throw std::out_of_range("Wave block index is out of range");
    }
    // 每个波保留完整 pointCapacity_ 跨距，而不是当前块 pointCount，布局在租约期内固定。
    return (waveIndex * pointCapacity_) + pointOffset;
}

}  // namespace vna::detail
