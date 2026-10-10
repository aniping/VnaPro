/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_TEST_RUNNER_H
#define VNAPRO_TESTS_TEST_RUNNER_H

#include <cstddef>
#include <functional>
#include <string>

namespace vna_test {

class TestRunner final {
public:
    // A runner starts with no results and owns the counters for this test process.
    TestRunner() noexcept = default;
    TestRunner(const TestRunner &) = delete;
    TestRunner &operator=(const TestRunner &) = delete;
    TestRunner(TestRunner &&) = delete;
    TestRunner &operator=(TestRunner &&) = delete;

    // Invokes the callable immediately; no capture is retained after this call.
    // Exceptions become failed cases and are reported to stderr rather than stopping later cases.
    void Run(const std::string &name, const std::function<void()> &function);
    // Prints all results and returns a process status: zero iff every executed case passed.
    int Finish() const;

private:
    std::size_t passed_ = 0U;
    std::size_t failed_ = 0U;
};

}  // namespace vna_test
#endif  // VNAPRO_TESTS_TEST_RUNNER_H
