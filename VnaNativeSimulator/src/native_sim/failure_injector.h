/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H
#define VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H

#include "vna_native/native_api_contract.h"

namespace vna_native {

enum class FailurePoint {
    BEFORE_FRAME_COMMIT,
    BEFORE_PENDING_COMMIT
};

class VNA_NATIVE_HIDDEN FailureInjector {
public:
    FailureInjector(const FailureInjector &) = delete;
    FailureInjector &operator=(const FailureInjector &) = delete;
    FailureInjector(FailureInjector &&) = delete;
    FailureInjector &operator=(FailureInjector &&) = delete;

    // Destruction through the interface releases the derived test fixture.
    virtual ~FailureInjector() = default;

    // Called under the state mutex before commit. Implementations must not reenter the state.
    virtual void ThrowIfRequested(FailurePoint point) = 0;

protected:
    FailureInjector() noexcept = default;
};

}  // namespace vna_native
#endif  // VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H
