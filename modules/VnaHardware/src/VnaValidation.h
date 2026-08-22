#ifndef VNA_HARDWARE_VNA_VALIDATION_H
#define VNA_HARDWARE_VNA_VALIDATION_H

#include "VnaCapabilities.h"
#include "VnaSweepPlan.h"

namespace vna::detail {

// 纯宿主校验，不调用驱动；用于在跨 ABI 前给出稳定的字段路径错误。
ValidationReport validatePlan(const VnaSweepPlan &plan, const VnaCapabilities &capabilities);

}  // namespace vna::detail

#endif
