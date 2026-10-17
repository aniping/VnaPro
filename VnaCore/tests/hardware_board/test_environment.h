/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_HARDWARE_BOARD_TEST_ENVIRONMENT_H
#define VNAPRO_HARDWARE_BOARD_TEST_ENVIRONMENT_H

#include <cstdint>
#include <string>
#include <vector>

namespace HardwareBoardTest {

enum class ExpectedPower {
    RAMP,
    ZERO,
    FRACTIONAL,
    BOUNDARIES
};

// 每项测试在加载探针前设置独立外部记录，不使用辅助导出函数操纵驱动内部状态。
class TestEnvironment final {
public:
    TestEnvironment(std::string trace, const std::string &failure = std::string(), std::uint32_t failAt = 1U,
        std::uint32_t gateAt = 0U, std::uint32_t failureCode = 731U);
    TestEnvironment(std::string trace, ExpectedPower power);
    ~TestEnvironment();
    TestEnvironment(const TestEnvironment &) = delete;
    TestEnvironment &operator=(const TestEnvironment &) = delete;
    std::vector<std::string> Events() const;
    void AwaitEvent(const std::string &event, std::size_t count = 1U) const;
    void ReleaseRead() const;
    // 在设备 Initialize 前设置独立探针取数延迟，不改变产品模拟器时序。
    void SetReadDelay(std::uint32_t milliseconds) const;
    void VerifyThreadAndEnvironment() const;

private:
    const std::string trace_;
    std::vector<std::string> saved_;
};

}  // 命名空间 HardwareBoardTest

#endif
