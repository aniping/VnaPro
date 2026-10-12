/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/plan_preparation_error.h"

namespace VnaCore {
namespace Detail {

PlanPreparationError::PlanPreparationError(const char *message) noexcept : message_(message)
{
}

const char *PlanPreparationError::what() const noexcept
{
    return message_;
}

}  // 命名空间 Detail
}  // 命名空间 VnaCore
