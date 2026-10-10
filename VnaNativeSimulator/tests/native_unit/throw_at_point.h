/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_NATIVE_UNIT_THROW_AT_POINT_H
#define VNAPRO_TESTS_NATIVE_UNIT_THROW_AT_POINT_H

#include <cstdint>
#include <stdexcept>

#include "failure_injector.h"

namespace VnaTest {

class ThrowAtPoint final : public VnaNative::FailureInjector {
public:
    // 选定一个提交阶段；计数包含每次匹配的调用。
    explicit ThrowAtPoint(VnaNative::FailurePoint point) noexcept : point_(point)
    {
    }

    ThrowAtPoint(const ThrowAtPoint &) = delete;
    ThrowAtPoint &operator=(const ThrowAtPoint &) = delete;
    ThrowAtPoint(ThrowAtPoint &&) = delete;
    ThrowAtPoint &operator=(ThrowAtPoint &&) = delete;
    ~ThrowAtPoint() override = default;

    // 在选定的提交前抛出异常，不回调已加锁的模拟器状态。
    void ThrowIfRequested(VnaNative::FailurePoint point) override
    {
        if (point == point_) {
            ++throwCount_;
            throw std::runtime_error("Injected native commit failure");
        }
    }

    // 返回匹配的调用次数；测试在同步操作返回后读取。
    std::uint32_t ThrowCount() const noexcept
    {
        return throwCount_;
    }

private:
    const VnaNative::FailurePoint point_;
    std::uint32_t throwCount_ = 0U;
};

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_NATIVE_UNIT_THROW_AT_POINT_H
