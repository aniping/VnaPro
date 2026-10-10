/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#define VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#include <exception>

namespace vna_core {
namespace Detail {

// This exception is reserved for failures before any plan-related device mutation.
class PlanPreparationError final : public std::exception {
public:
    // Distinguishes allocation failure before device mutation from native configuration failures.
    const char *what() const noexcept override;
};

}  // namespace Detail
}  // namespace vna_core

#endif
