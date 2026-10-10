/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "test_support.h"

#include <cmath>
#include <sstream>

#include "test_failure.h"

namespace VnaTest {

void Fail(const std::string &message, TestLocation location)
{
    std::ostringstream stream;
    stream << location.file << ':' << location.line << ": " << message;
    throw TestFailure(stream.str());
}

void Expect(bool condition, TestLocation location)
{
    if (!condition) {
        Fail("Expectation failed; see the condition at this file and line", location);
    }
}

void ExpectNear(double expected, double actual, double tolerance, TestLocation location)
{
    if (!std::isfinite(expected) || !std::isfinite(actual) || !std::isfinite(tolerance) || tolerance < 0.0 ||
        std::fabs(expected - actual) > tolerance) {
        Fail("Actual " + ToString(actual) + ", expected " + ToString(expected) + ", tolerance " + ToString(tolerance),
            location);
    }
}

std::string ToString(std::size_t value)
{
    return std::to_string(value);
}

std::string ToString(double value)
{
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

}  // namespace VnaTest
