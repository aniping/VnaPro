#ifndef VNA_HARDWARE_VNA_CAPABILITIES_H
#define VNA_HARDWARE_VNA_CAPABILITIES_H

#include <cstddef>
#include <cstdint>

namespace vna {

struct NumericRange {
    double minimum{0.0};
    double maximum{0.0};
};

// 连接成功后从驱动复制的能力快照，计划验证只依赖此快照。
struct VnaCapabilities {
    // 基本测量范围；上下界均包含在有效区间内。
    std::uint16_t portCount{0};
    NumericRange frequencyHz;
    NumericRange ifBandwidthHz;
    NumericRange powerDbm;
    // 维度上限用于在跨 ABI 前拒绝不可表示或过大的计划。
    std::size_t maximumPoints{0};
    std::size_t maximumExcitationsPerPoint{0};
    std::size_t preferredBatchPoints{0};
    // 细粒度能力位决定计划的哪些字段允许逐点变化。
    bool perPointIfBandwidth{false};
    bool perPointPower{false};
    bool perPointDwell{false};
    bool simultaneousSources{false};
    // 操作能力位与驱动回调必须保持一致。
    bool supportsAbort{false};
    bool supportsInternalTrigger{false};
    bool supportsExternalTrigger{false};
};

}  // namespace vna

#endif
