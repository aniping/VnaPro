/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_SWEEP_PLAN_VALIDATION_H
#define VNAPRO_VNA_CORE_SWEEP_PLAN_VALIDATION_H

#include <string>

#include "vna_core/sweep_plan.h"

namespace VnaCore {
namespace Detail {

// 在设备调用前验证完整计划的数值、长度和缓冲预算；成功返回空字符串。
std::string ValidateSweepPlan(const SweepPlan &plan);

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
