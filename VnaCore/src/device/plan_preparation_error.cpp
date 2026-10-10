/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "device/plan_preparation_error.h"

namespace VnaCore {
namespace Detail {

const char *PlanPreparationError::what() const noexcept
{
    return "device plan buffer preparation failed";
}

}  // namespace Detail
}  // namespace VnaCore
