/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H
#define VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H

#include "normal_frame_snapshot.h"

namespace VnaNative {

struct VNA_NATIVE_HIDDEN NativeFrameSlot final {
    bool valid = false;
    NormalFrameSnapshot frame;

    // 发布或清空整个槽位，不进行内存分配；调用者必须持有状态互斥锁。
    void Swap(NativeFrameSlot &other) noexcept;
};

}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H
