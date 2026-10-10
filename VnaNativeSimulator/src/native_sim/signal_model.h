/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
#define VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H

#include <cstdint>
#include <vector>

#include "normal_frame_snapshot.h"

namespace vna_native {
namespace SignalModel {

// Replaces output only on success; each point contains [A.re,A.im,B.re,B.im] for ports 0..3.
// The complete port layout is retained regardless of RX masks; raw IFBW/power fields do not affect IQ.
VNA_NATIVE_HIDDEN std::uint32_t Generate(const NormalFrameSnapshot &frame, std::vector<double> &output) noexcept;

// Adds deterministic complex noise bounded by 0.2% of each golden wave magnitude.
// Same seed and successful Start sequence reproduce the result; errors leave output unchanged.
VNA_NATIVE_HIDDEN std::uint32_t GenerateNoisy(const NormalFrameSnapshot &frame, std::uint64_t seed,
    std::uint64_t sweepIndex, std::vector<double> &output) noexcept;

}  // namespace SignalModel
}  // namespace vna_native
#endif  // VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
