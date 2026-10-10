#ifndef VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
#define VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H

#include <cstdint>
#include <vector>

#include "normal_frame_decoder.h"

namespace vna_native {

// 为普通帧生成可重复的测试 IQ，用端口和频率区分数据，不代表真实射频响应。
// 每点固定 16 个 double：按端口 0~3 依次写入 [A实部, A虚部, B实部, B虚部]。
class VNA_NATIVE_HIDDEN SignalModel {
public:
    // 成功时一次性替换 output；失败时 output 保持不变。
    // 输出布局不随接收掩码裁剪；IFBW 和不透明功率表目前均不影响数值。
    static std::uint32_t Generate(const NormalFrameSnapshot& frame, std::vector<double>* output);
    // 在黄金值上叠加确定性复数噪声，幅度不超过原波幅的 0.2%；同种子和序号可复现。
    // sweepIndex 由状态层按成功 Start 递增，Get 只读取已生成的快照。
    static std::uint32_t GenerateNoisy(
        const NormalFrameSnapshot& frame, std::uint64_t seed, std::uint64_t sweepIndex, std::vector<double>* output);
};

}  // namespace vna_native

#endif  // VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
