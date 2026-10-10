/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "test_runner.h"

#include <exception>
#include <iostream>

namespace vna_test {

void TestRunner::Run(const std::string &name, const std::function<void()> &function)
{
    try {
        function();
        ++passed_;
        std::cout << "[通过] " << name << '\n';
    } catch (const std::exception &error) {
        ++failed_;
        std::cerr << "[失败] " << name << "\n  " << error.what() << '\n';
    } catch (...) {
        ++failed_;
        std::cerr << "[失败] " << name << "\n  未知异常\n";
    }
}

int TestRunner::Finish() const
{
    std::cout << "测试汇总：" << passed_ << " 通过，" << failed_ << " 失败\n";
    return failed_ == 0U ? 0 : 1;
}

}  // namespace vna_test
