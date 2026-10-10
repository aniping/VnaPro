/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_frame_slot.h"

#include <utility>

namespace vna_native {

void NativeFrameSlot::Swap(NativeFrameSlot &other) noexcept
{
    std::swap(valid, other.valid);
    frame.Swap(other.frame);
}

}  // namespace vna_native
