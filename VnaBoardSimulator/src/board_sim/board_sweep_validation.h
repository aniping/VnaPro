/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_BOARD_SWEEP_VALIDATION_H
#define VNAPRO_BOARD_SWEEP_VALIDATION_H

#include <cstdint>

#include "board_sweep_types.h"

namespace VnaBoard {

// 校验新的暂存副本并准备 IQ 布局；成功时原生工作端口 1～4 转为内部下标 0～3。
// 失败不修改候选布局或端口；成功后的候选不能再次作为原生暂存副本传入。
std::uint32_t ValidatePlan(SweepPlan &plan);

}  // 命名空间 VnaBoard
#endif
