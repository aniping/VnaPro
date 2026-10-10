/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "native_frame_slot.h"

#include <utility>

namespace VnaNative {

void NativeFrameSlot::Swap(NativeFrameSlot &other) noexcept
{
    std::swap(valid, other.valid);
    frame.Swap(other.frame);
}

}  // 命名空间 VnaNative
