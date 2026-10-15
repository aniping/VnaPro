/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_BOARD_SWEEP_VALIDATION_H
#define VNAPRO_BOARD_SWEEP_VALIDATION_H

#include <cstdint>

#include "board_sweep_types.h"

namespace VnaBoard {

// 校验候选列表和预算，通过后填入完整 IQ 布局；失败不改变布局字段。
std::uint32_t ValidatePlan(SweepPlan &plan);

}  // 命名空间 VnaBoard
#endif
