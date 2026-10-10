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
    // Selects one commit stage; counting includes every matching invocation.
    explicit ThrowAtPoint(VnaNative::FailurePoint point) noexcept : point_(point)
    {
    }

    ThrowAtPoint(const ThrowAtPoint &) = delete;
    ThrowAtPoint &operator=(const ThrowAtPoint &) = delete;
    ThrowAtPoint(ThrowAtPoint &&) = delete;
    ThrowAtPoint &operator=(ThrowAtPoint &&) = delete;
    ~ThrowAtPoint() override = default;

    // Throws before the selected commit without calling back into the locked simulator state.
    void ThrowIfRequested(VnaNative::FailurePoint point) override
    {
        if (point == point_) {
            ++throwCount_;
            throw std::runtime_error("Injected native commit failure");
        }
    }

    // Returns matching invocations; tests read this after the synchronous operation returns.
    std::uint32_t ThrowCount() const noexcept
    {
        return throwCount_;
    }

private:
    const VnaNative::FailurePoint point_;
    std::uint32_t throwCount_ = 0U;
};

}  // namespace VnaTest
#endif  // VNAPRO_TESTS_NATIVE_UNIT_THROW_AT_POINT_H
