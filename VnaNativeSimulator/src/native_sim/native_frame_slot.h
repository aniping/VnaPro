/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H
#define VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H

#include "normal_frame_snapshot.h"

namespace vna_native {

struct VNA_NATIVE_HIDDEN NativeFrameSlot final {
    bool valid = false;
    NormalFrameSnapshot frame;

    // Publishes or clears an entire slot without allocating; callers hold the state mutex.
    void Swap(NativeFrameSlot &other) noexcept;
};

}  // namespace vna_native
#endif  // VNAPRO_NATIVE_SIM_NATIVE_FRAME_SLOT_H
