/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_SWEEP_PROGRESS_H
#define VNAPRO_VNA_CORE_SWEEP_PROGRESS_H

#include <cstddef>
#include <memory>

#include "vna_core/measurement_frame.h"

namespace VnaCore {

// 仅供显示的不可变扫描快照；不会推进完整帧序号，也不能用于标记、远程取数或校准采集。
struct SweepProgress final {
    // 保留完整计划及 IQ 容量，未采集后缀为 NaN；所有源仅有 completedPoints 个完整前缀点。
    FramePtr frame;
    std::size_t completedPoints = 0U;
};

using SweepProgressPtr = std::shared_ptr<const SweepProgress>;

}  // 命名空间 VnaCore

#endif
