/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_TEST_FAILURE_H
#define VNAPRO_TESTS_TEST_FAILURE_H

#include <stdexcept>
#include <string>

namespace VnaTest {

class TestFailure final : public std::runtime_error {
public:
    // Owns the failure message reported by the test runner.
    explicit TestFailure(const std::string &message);
    TestFailure(const TestFailure &) = default;
    TestFailure &operator=(const TestFailure &) = default;
    TestFailure(TestFailure &&) noexcept = default;
    TestFailure &operator=(TestFailure &&) noexcept = default;
    ~TestFailure() override = default;
};

}  // namespace VnaTest
#endif  // VNAPRO_TESTS_TEST_FAILURE_H
