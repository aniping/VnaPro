#include "VnaFrameTracker.h"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace vna::detail {

VnaFrameTracker::VnaFrameTracker(const VnaSweepPlan &plan, const std::vector<VnaWaveLayout> &layouts)
    : pointCount_(plan.points.size())
{
    if (pointCount_ > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Sweep point count exceeds the frame index range");
    }
    initializeLayouts(layouts);
    compilePlan(plan);
}

void VnaFrameTracker::initializeLayouts(const std::vector<VnaWaveLayout> &layouts)
{
    if (layouts.empty()) {
        throw std::invalid_argument("Frame tracker requires at least one layout");
    }
    layouts_.reserve(layouts.size());
    for (const auto &layout : layouts) {
        if (layout.layoutId == 0) {
            throw std::invalid_argument("Frame tracker received an invalid layout identifier");
        }
        layouts_.push_back(LayoutFrames{layout.layoutId, layout.excitationId, {}, {}, 0});
    }
    std::sort(layouts_.begin(), layouts_.end(), [](const auto &left, const auto &right) {
        return left.layoutId < right.layoutId;
    });
    for (std::size_t index = 1; index < layouts_.size(); ++index) {
        if (layouts_[index - 1].layoutId == layouts_[index].layoutId) {
            throw std::invalid_argument("Frame tracker received duplicate layout identifiers");
        }
    }
}

void VnaFrameTracker::compilePlan(const VnaSweepPlan &plan)
{
    std::map<ExcitationId, LayoutFrames *> layoutsByExcitation;
    for (auto &layout : layouts_) {
        if (!layoutsByExcitation.emplace(layout.excitationId, &layout).second) {
            throw std::invalid_argument("Frame tracker received duplicate excitation layouts");
        }
    }
    for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
        for (const auto &excitation : plan.points[pointIndex].excitations) {
            const auto layout = layoutsByExcitation.find(excitation.excitationId);
            if (layout == layoutsByExcitation.end()) {
                throw std::invalid_argument("Frame tracker cannot map a planned excitation");
            }
            appendExpectedFrame(*layout->second, static_cast<std::uint32_t>(pointIndex));
        }
    }
}

void VnaFrameTracker::appendExpectedFrame(LayoutFrames &frames, std::uint32_t pointIndex)
{
    // 同一点同一激励只能对应一个帧；重复项说明 Prepare 前置校验被绕过。
    if (!frames.expectedPointIndices.empty() && (frames.expectedPointIndices.back() == pointIndex)) {
        throw std::invalid_argument("Frame tracker received duplicate excitations at one point");
    }
    if (expectedFrameCount_ == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("Expected frame count is not representable");
    }
    frames.expectedPointIndices.push_back(pointIndex);
    frames.reservations.push_back(0);
    ++expectedFrameCount_;
}

VnaFrameTracker::LayoutFrames *VnaFrameTracker::findLayout(LayoutId layoutId)
{
    const auto match = std::lower_bound(
        layouts_.begin(), layouts_.end(), layoutId,
        [](const LayoutFrames &frames, LayoutId identifier) { return frames.layoutId < identifier; });
    return ((match != layouts_.end()) && (match->layoutId == layoutId)) ? &*match : nullptr;
}

bool VnaFrameTracker::validPlanRange(std::uint32_t firstPointIndex, std::uint32_t pointCount) const
{
    const std::size_t first = firstPointIndex;
    const std::size_t count = pointCount;
    // 先用减法证明范围有效，避免 first + count 的无符号溢出。
    return (count > 0) && (first < pointCount_) && (count <= (pointCount_ - first));
}

bool VnaFrameTracker::locateExpectedRange(
    const LayoutFrames &frames, std::uint32_t firstPointIndex, std::uint32_t pointCount, std::size_t &rangeOffset) const
{
    const auto first =
        std::lower_bound(frames.expectedPointIndices.begin(), frames.expectedPointIndices.end(), firstPointIndex);
    rangeOffset = static_cast<std::size_t>(std::distance(frames.expectedPointIndices.begin(), first));
    if (pointCount > (frames.expectedPointIndices.size() - rangeOffset)) {
        return false;
    }
    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto expectedPoint = firstPointIndex + static_cast<std::uint32_t>(index);
        if (frames.expectedPointIndices[rangeOffset + index] != expectedPoint) {
            return false;
        }
    }
    return true;
}

bool VnaFrameTracker::hasReservationState(
    const LayoutFrames &frames, std::size_t rangeOffset, std::uint32_t pointCount, std::uint8_t state)
{
    const auto first = frames.reservations.begin() + static_cast<std::ptrdiff_t>(rangeOffset);
    const auto last = first + static_cast<std::ptrdiff_t>(pointCount);
    return std::all_of(first, last, [state](std::uint8_t value) { return value == state; });
}

void VnaFrameTracker::setReservationState(
    LayoutFrames &frames, std::size_t rangeOffset, std::uint32_t pointCount, std::uint8_t state)
{
    auto first = frames.reservations.begin() + static_cast<std::ptrdiff_t>(rangeOffset);
    std::fill_n(first, pointCount, state);
}

VnaFrameReservationResult VnaFrameTracker::reserve(
    LayoutId layoutId, std::uint32_t firstPointIndex, std::uint32_t pointCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    LayoutFrames *frames = findLayout(layoutId);
    if (frames == nullptr) {
        return VnaFrameReservationResult::UnknownLayout;
    }
    if (!validPlanRange(firstPointIndex, pointCount)) {
        return VnaFrameReservationResult::InvalidRange;
    }
    std::size_t rangeOffset = 0;
    if (!locateExpectedRange(*frames, firstPointIndex, pointCount, rangeOffset)) {
        return VnaFrameReservationResult::UnexpectedPoint;
    }
    if (!hasReservationState(*frames, rangeOffset, pointCount, 0)) {
        return VnaFrameReservationResult::DuplicateFrame;
    }
    // 每个布局按计划点序提交；驱动必须在跨 ABI 前完成硬件乱序结果的重排。
    if (rangeOffset != frames->nextReservationOffset) {
        return VnaFrameReservationResult::OutOfOrderFrame;
    }
    setReservationState(*frames, rangeOffset, pointCount, 1);
    frames->nextReservationOffset += pointCount;
    reservedFrameCount_ += pointCount;
    return VnaFrameReservationResult::Reserved;
}

bool VnaFrameTracker::release(LayoutId layoutId, std::uint32_t firstPointIndex, std::uint32_t pointCount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    LayoutFrames *frames = findLayout(layoutId);
    std::size_t rangeOffset = 0;
    const bool located = (frames != nullptr) && validPlanRange(firstPointIndex, pointCount) &&
                         locateExpectedRange(*frames, firstPointIndex, pointCount, rangeOffset);
    const bool isLatestRange = located && ((rangeOffset + pointCount) == frames->nextReservationOffset) &&
                               hasReservationState(*frames, rangeOffset, pointCount, 1);
    if (!isLatestRange) {
        return false;
    }
    setReservationState(*frames, rangeOffset, pointCount, 0);
    frames->nextReservationOffset = rangeOffset;
    reservedFrameCount_ -= pointCount;
    return true;
}

std::size_t VnaFrameTracker::expectedFrameCount() const
{
    return expectedFrameCount_;
}

std::size_t VnaFrameTracker::reservedFrameCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return reservedFrameCount_;
}

bool VnaFrameTracker::complete() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return reservedFrameCount_ == expectedFrameCount_;
}

}  // namespace vna::detail
