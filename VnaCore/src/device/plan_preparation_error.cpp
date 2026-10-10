/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/plan_preparation_error.h"

namespace vna_core {
namespace detail {

const char *PlanPreparationError::what() const noexcept
{
    return "device plan buffer preparation failed";
}

}  // namespace detail
}  // namespace vna_core
