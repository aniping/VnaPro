/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H
#define VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H

#include <stdexcept>

#include "failure_injector.h"

namespace vna_test {

class RejectPendingCommit final : public VnaNative::FailureInjector {
public:
    // A stateless fixture that leaves frame download commits unaffected.
    RejectPendingCommit() noexcept = default;
    RejectPendingCommit(const RejectPendingCommit &) = delete;
    RejectPendingCommit &operator=(const RejectPendingCommit &) = delete;
    RejectPendingCommit(RejectPendingCommit &&) = delete;
    RejectPendingCommit &operator=(RejectPendingCommit &&) = delete;
    ~RejectPendingCommit() override = default;

    // Fails only pending publication, allowing tests to check that noise sequence is unchanged.
    void ThrowIfRequested(VnaNative::FailurePoint point) override
    {
        if (point == VnaNative::FailurePoint::BEFORE_PENDING_COMMIT) {
            throw std::runtime_error("Injected noisy sweep publication failure");
        }
    }
};

}  // namespace vna_test
#endif  // VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H
