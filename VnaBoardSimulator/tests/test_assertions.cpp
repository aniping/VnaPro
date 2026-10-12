/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <cstdlib>
#include <stdexcept>

#include "test_support.h"

namespace BoardTest {

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void SetEnvironment(const char *name, const char *value)
{
#if defined(_WIN32)
    Require(_putenv_s(name, value) == 0, "环境变量设置失败");
#else
    Require(setenv(name, value, 1) == 0, "环境变量设置失败");
#endif
}

}  // 命名空间 BoardTest
