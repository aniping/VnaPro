#ifndef VNAPRO_TESTS_TEST_SUPPORT_H
#define VNAPRO_TESTS_TEST_SUPPORT_H

#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vna_test {

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

inline void Fail(const char* file, int line, const std::string& message) {
    std::ostringstream stream;
    stream << file << ':' << line << ": " << message;
    throw TestFailure(stream.str());
}

template <typename T>
std::string ToString(const T& value) {
    std::ostringstream stream;
    stream << value;
    return stream.str();
}

inline std::string ToString(const char* value) {
    return value == NULL ? "<null>" : std::string(value);
}

class TestRunner {
public:
    TestRunner() : passed_(0U), failed_(0U) {}

    template <typename Function>
    void Run(const std::string& name, Function function) {
        try {
            function();
            ++passed_;
            std::cout << "[通过] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed_;
            std::cerr << "[失败] " << name << "\n  " << error.what() << '\n';
        } catch (...) {
            ++failed_;
            std::cerr << "[失败] " << name << "\n  未知异常\n";
        }
    }

    int Finish() const {
        std::cout << "测试汇总：" << passed_ << " 通过，" << failed_ << " 失败\n";
        return failed_ == 0U ? 0 : 1;
    }

private:
    std::size_t passed_;
    std::size_t failed_;
};

typedef void (*TestFunction)();

struct RegisteredTest {
    RegisteredTest(const char* testName, TestFunction testFunction)
        : name(testName), function(testFunction) {}

    const char* name;
    TestFunction function;
};

inline std::vector<RegisteredTest>& TestRegistry() {
    static std::vector<RegisteredTest> registry;
    return registry;
}

class TestRegistrar {
public:
    TestRegistrar(const char* name, TestFunction function) {
        TestRegistry().push_back(RegisteredTest(name, function));
    }
};

inline int RunRegisteredTests() {
    TestRunner runner;
    const std::vector<RegisteredTest>& tests = TestRegistry();
    for (std::size_t index = 0U; index < tests.size(); ++index) {
        const RegisteredTest& test = tests[index];
        runner.Run(test.name, test.function);
    }
    return runner.Finish();
}

}  // namespace vna_test

#define VNA_TEST(test_name)                                                   \
    static void test_name();                                                  \
    namespace {                                                               \
    const ::vna_test::TestRegistrar test_name##_registrar(#test_name,          \
                                                           &test_name);        \
    }                                                                         \
    static void test_name()

#define VNA_FAIL(message)                                                      \
    ::vna_test::Fail(__FILE__, __LINE__, (message))

#define VNA_EXPECT_TRUE(condition)                                             \
    do {                                                                       \
        if (!(condition)) {                                                    \
            ::vna_test::Fail(__FILE__, __LINE__,                               \
                             "期望为真：" #condition);                        \
        }                                                                      \
    } while (false)

#define VNA_EXPECT_FALSE(condition)                                            \
    do {                                                                       \
        if (condition) {                                                       \
            ::vna_test::Fail(__FILE__, __LINE__,                               \
                             "期望为假：" #condition);                        \
        }                                                                      \
    } while (false)

#define VNA_EXPECT_EQ(expected, actual)                                        \
    do {                                                                       \
        const auto vna_expected_value = (expected);                            \
        const auto vna_actual_value = (actual);                                \
        if (!(vna_expected_value == vna_actual_value)) {                       \
            ::vna_test::Fail(                                                  \
                __FILE__, __LINE__,                                            \
                std::string("期望 ") + #actual + " == " #expected +         \
                    "，实际为 " + ::vna_test::ToString(vna_actual_value) +    \
                    "，期望值为 " +                                           \
                    ::vna_test::ToString(vna_expected_value));                 \
        }                                                                      \
    } while (false)

#define VNA_EXPECT_NE(not_expected, actual)                                    \
    do {                                                                       \
        const auto vna_not_expected_value = (not_expected);                    \
        const auto vna_actual_value = (actual);                                \
        if (vna_not_expected_value == vna_actual_value) {                      \
            ::vna_test::Fail(                                                  \
                __FILE__, __LINE__,                                            \
                std::string("期望 ") + #actual + " != " #not_expected +     \
                    "，两者均为 " +                                           \
                    ::vna_test::ToString(vna_actual_value));                   \
        }                                                                      \
    } while (false)

#define VNA_EXPECT_NEAR(expected, actual, tolerance)                           \
    do {                                                                       \
        const double vna_expected_value = static_cast<double>(expected);       \
        const double vna_actual_value = static_cast<double>(actual);           \
        const double vna_tolerance_value = static_cast<double>(tolerance);     \
        if (!std::isfinite(vna_actual_value) ||                                \
            std::fabs(vna_expected_value - vna_actual_value) >                 \
                vna_tolerance_value) {                                         \
            ::vna_test::Fail(                                                  \
                __FILE__, __LINE__,                                            \
                std::string("数值不在容差内，实际为 ") +                      \
                    ::vna_test::ToString(vna_actual_value) +                   \
                    "，期望值为 " +                                           \
                    ::vna_test::ToString(vna_expected_value) +                 \
                    "，容差为 " +                                             \
                    ::vna_test::ToString(vna_tolerance_value));                \
        }                                                                      \
    } while (false)

#endif  // VNAPRO_TESTS_TEST_SUPPORT_H
