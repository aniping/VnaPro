/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
#define VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H

#include <cstdint>
#include <vector>

#include "normal_frame_snapshot.h"

namespace VnaNative {
namespace SignalModel {

// 仅在成功时替换输出；每个点包含端口 0～3 各自的 [A.re,A.im,B.re,B.im]。
// 无论接收位图如何设置，均保留完整端口布局；原始中频带宽和功率字段不影响 IQ。
VNA_NATIVE_HIDDEN std::uint32_t Generate(const NormalFrameSnapshot &frame, std::vector<double> &output) noexcept;

// 添加可复现的复数噪声，其模长不超过各基准波幅值乘以 noiseAmplitude。
// noiseAmplitude 接受有限的 [0,1]，默认 0.002，即 0.2%；零幅度等同黄金向量。
// 相同种子和成功启动序列可复现结果；失败时保持输出不变。
VNA_NATIVE_HIDDEN std::uint32_t GenerateNoisy(const NormalFrameSnapshot &frame, std::uint64_t seed,
    std::uint64_t sweepIndex, std::vector<double> &output, double noiseAmplitude = 0.002) noexcept;

}  // 命名空间 SignalModel
}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
