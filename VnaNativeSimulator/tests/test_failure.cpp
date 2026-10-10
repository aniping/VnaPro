/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "test_failure.h"

namespace vna_test {

TestFailure::TestFailure(const std::string &message) : std::runtime_error(message)
{
}

}  // namespace vna_test
