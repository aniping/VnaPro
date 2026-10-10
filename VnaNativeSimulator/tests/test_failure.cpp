/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "test_failure.h"

namespace VnaTest {

TestFailure::TestFailure(const std::string &message) : std::runtime_error(message)
{
}

}  // 命名空间 VnaTest
