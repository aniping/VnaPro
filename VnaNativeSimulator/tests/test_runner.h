/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_TEST_RUNNER_H
#define VNAPRO_TESTS_TEST_RUNNER_H

#include <cstddef>
#include <functional>
#include <string>

namespace VnaTest {

class TestRunner final {
public:
    // 执行器初始没有测试结果，并持有本测试进程的计数器。
    TestRunner() noexcept = default;
    TestRunner(const TestRunner &) = delete;
    TestRunner &operator=(const TestRunner &) = delete;
    TestRunner(TestRunner &&) = delete;
    TestRunner &operator=(TestRunner &&) = delete;

    // 立即执行可调用对象；调用结束后不保留其捕获内容。
    // 异常记为失败用例并输出到标准错误，后续用例继续执行。
    void Run(const std::string &name, const std::function<void()> &function);
    // 输出所有结果并返回进程状态；仅当全部已执行用例通过时返回零。
    int Finish() const;

private:
    std::size_t passed_ = 0U;
    std::size_t failed_ = 0U;
};

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_TEST_RUNNER_H
