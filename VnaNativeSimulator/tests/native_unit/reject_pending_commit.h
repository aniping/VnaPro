/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H
#define VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H

#include <stdexcept>

#include "failure_injector.h"

namespace VnaTest {

class RejectPendingCommit final : public VnaNative::FailureInjector {
public:
    // 无状态的测试夹具，不影响帧下载提交。
    RejectPendingCommit() noexcept = default;
    RejectPendingCommit(const RejectPendingCommit &) = delete;
    RejectPendingCommit &operator=(const RejectPendingCommit &) = delete;
    RejectPendingCommit(RejectPendingCommit &&) = delete;
    RejectPendingCommit &operator=(RejectPendingCommit &&) = delete;
    ~RejectPendingCommit() override = default;

    // 仅使待取结果发布失败，供测试检查噪声序列未发生变化。
    void ThrowIfRequested(VnaNative::FailurePoint point) override
    {
        if (point == VnaNative::FailurePoint::BEFORE_PENDING_COMMIT) {
            throw std::runtime_error("Injected noisy sweep publication failure");
        }
    }
};

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_NATIVE_UNIT_REJECT_PENDING_COMMIT_H
