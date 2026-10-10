/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_TEST_SUPPORT_H
#define VNAPRO_TESTS_TEST_SUPPORT_H

#include <cstddef>
#include <string>

#include "test_runner.h"

namespace vna_test {

struct TestLocation final {
    const char *file = nullptr;
    int line = 0;
};

// Fails with the caller's file/line. Explicit locations avoid function-style assertion macros.
void Fail(const std::string &message, TestLocation location);
// Throws TestFailure when condition is false; callers pass an explicit C++14 source location.
void Expect(bool condition, TestLocation location);
// Requires a finite actual value within a nonnegative absolute tolerance.
void ExpectNear(double expected, double actual, double tolerance, TestLocation location);
std::string ToString(std::size_t value);
std::string ToString(double value);

}  // namespace vna_test
#endif  // VNAPRO_TESTS_TEST_SUPPORT_H
