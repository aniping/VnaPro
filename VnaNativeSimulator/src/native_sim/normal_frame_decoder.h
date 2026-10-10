/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H

#include <cstdint>

#include "normal_frame_snapshot.h"

namespace vna_native {
namespace NormalFrameDecoder {

// Validates pointers and declared element counts without reading either borrowed buffer.
// Counts must exactly match the frame layout; actual capacity remains the caller's duty.
VNA_NATIVE_HIDDEN std::uint32_t ValidateInputShape(const NormalFrameInput &input) noexcept;

// On success replaces output with a deep copy. On error output stays unchanged.
// Checks all source ports before unsupported mode flags; callers provide synchronization.
VNA_NATIVE_HIDDEN std::uint32_t Decode(const NormalFrameInput &input, NormalFrameSnapshot &output) noexcept;

}  // namespace NormalFrameDecoder
}  // namespace vna_native
#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
