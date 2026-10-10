/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_TEST_SUPPORT_H
#define VNAPRO_TESTS_TEST_SUPPORT_H

#include <cstddef>
#include <string>

#include "test_runner.h"

namespace VnaTest {

struct TestLocation final {
    const char *file = nullptr;
    int line = 0;
};

// 失败时报告调用者的文件和行号；显式传入位置，无需使用函数式断言宏。
void Fail(const std::string &message, TestLocation location);
// condition 为假时抛出 TestFailure；调用者显式传入 C++14 源码位置。
void Expect(bool condition, TestLocation location);
// 实际值必须有限，且误差不超过给定的非负绝对容差。
void ExpectNear(double expected, double actual, double tolerance, TestLocation location);
std::string ToString(std::size_t value);
std::string ToString(double value);

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_TEST_SUPPORT_H
