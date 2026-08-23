#ifndef VNA_HARDWARE_VNA_FRAME_TRACKER_H
#define VNA_HARDWARE_VNA_FRAME_TRACKER_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "VnaSweepPlan.h"

namespace vna::detail {

// 预留结果区分驱动协议错误，调用方可据此记录一次低频故障日志。
enum class VnaFrameReservationResult : std::uint8_t {
    Reserved,
    UnknownLayout,
    InvalidRange,
    UnexpectedPoint,
    DuplicateFrame,
    OutOfOrderFrame
};

// Prepare 后预编译合法帧身份；扫描热路径只进行二分查找和原地标记。
class VnaFrameTracker final {
public:
    VnaFrameTracker(const VnaSweepPlan &plan, const std::vector<VnaWaveLayout> &layouts);
    VnaFrameTracker(const VnaFrameTracker &) = delete;
    VnaFrameTracker &operator=(const VnaFrameTracker &) = delete;

    // 仅在上一 Execute 已返回且旧 Sweep 路由已注销后调用；保留预编译拓扑和容器容量。
    void resetForSweep();
    VnaFrameReservationResult reserve(LayoutId layoutId, std::uint32_t firstPointIndex, std::uint32_t pointCount);
    // 仅用于下游发布失败后的原范围回滚；未完整预留的范围不会被部分修改。
    bool release(LayoutId layoutId, std::uint32_t firstPointIndex, std::uint32_t pointCount);
    std::size_t expectedFrameCount() const;
    std::size_t reservedFrameCount() const;
    bool complete() const;

private:
    struct LayoutFrames {
        LayoutId layoutId{0};
        ExcitationId excitationId{0};
        std::vector<std::uint32_t> expectedPointIndices;
        std::vector<std::uint8_t> reservations;
        std::size_t nextReservationOffset{0};
    };

    void initializeLayouts(const std::vector<VnaWaveLayout> &layouts);
    void compilePlan(const VnaSweepPlan &plan);
    void appendExpectedFrame(LayoutFrames &frames, std::uint32_t pointIndex);
    LayoutFrames *findLayout(LayoutId layoutId);
    bool validPlanRange(std::uint32_t firstPointIndex, std::uint32_t pointCount) const;
    bool locateExpectedRange(
        const LayoutFrames &frames, std::uint32_t firstPointIndex, std::uint32_t pointCount,
        std::size_t &rangeOffset) const;
    static bool hasReservationState(
        const LayoutFrames &frames, std::size_t rangeOffset, std::uint32_t pointCount, std::uint8_t state);
    static void setReservationState(
        LayoutFrames &frames, std::size_t rangeOffset, std::uint32_t pointCount, std::uint8_t state);

    std::vector<LayoutFrames> layouts_;
    std::size_t pointCount_{0};
    std::size_t expectedFrameCount_{0};
    std::size_t reservedFrameCount_{0};
    // Execute 单生产者提交；互斥量同时保护分发失败回滚和 Actor 的完成快照查询。
    mutable std::mutex mutex_;
};

}  // namespace vna::detail

#endif
