/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_DISPLAY_MARKER_COLLECTION_H
#define VNAPRO_VNA_DISPLAY_MARKER_COLLECTION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VnaDisplay {

constexpr unsigned MAX_MARKERS = 10U;

struct FrameContext;

struct MarkerSettings final {
    bool enabled = false;
    bool hasFrequency = false;
    double frequencyHz = 0.0;
    bool hasSampleIndex = false;
    std::size_t sampleIndex = 0U;
    // 频率输入默认采用复数插值，原始下标输入明确选择离散采样点。
    bool interpolate = true;
    std::uint64_t acquisitionRevision = 0U;
};

enum class MarkerAction {
    UPDATE,
    ADD,
    SELECT,
    ALL_OFF
};

struct MarkerChange final {
    MarkerAction action = MarkerAction::UPDATE;
    // 0 表示执行时的活动标记；外部明确指定编号时范围为 1..10。
    unsigned id = 0U;
    bool hasEnabled = false;
    bool enabled = false;
    bool hasFrequency = false;
    double frequencyHz = 0.0;
    bool hasSampleIndex = false;
    std::size_t sampleIndex = 0U;
    bool hasStep = false;
    int step = 0;
    bool hasInterpolation = false;
    bool interpolate = true;
    bool checkAcquisitionRevision = false;
    std::uint64_t expectedAcquisitionRevision = 0U;
};

// 每条迹线独立拥有标记集合；关闭保留设置，删除迹线才丢弃集合。
class MarkerCollection final {
public:
    unsigned Selected() const noexcept;
    MarkerSettings &Active() noexcept;
    const MarkerSettings &Active() const noexcept;
    const MarkerSettings *Find(unsigned id) const noexcept;
    const std::array<MarkerSettings, MAX_MARKERS> &Items() const noexcept;
    bool SameSettings(const MarkerCollection &other) const noexcept;
    // 先校验候选副本，失败不改变编号选择或任何标记属性。
    bool Apply(const MarkerChange &change, const FrameContext &context, std::string &error);
    void ReconcileAxis(const std::vector<double> &axis, bool preserveSampleIndexes) noexcept;
    void BindAcquisitionRevision(std::uint64_t acquisitionRevision) noexcept;

private:
    std::array<MarkerSettings, MAX_MARKERS> items_{};
    unsigned selected_ = 1U;
};

}  // 命名空间 VnaDisplay

#endif
