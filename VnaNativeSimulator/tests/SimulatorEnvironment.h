#ifndef VNAPRO_TESTS_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_TESTS_SIMULATOR_ENVIRONMENT_H

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace vna_test {

// 测试串行修改环境；离开作用域恢复原值，避免噪声用例污染黄金向量用例。
class ScopedSimulatorEnvironment {
public:
    ScopedSimulatorEnvironment(const char* mode, const char* seed)
        : hadMode_(std::getenv("VNAPRO_SIM_MODE") != NULL),
          oldMode_(hadMode_ ? std::getenv("VNAPRO_SIM_MODE") : ""),
          hadSeed_(std::getenv("VNAPRO_SIM_SEED") != NULL),
          oldSeed_(hadSeed_ ? std::getenv("VNAPRO_SIM_SEED") : "")
    {
        Set(mode, seed);
    }

    ~ScopedSimulatorEnvironment()
    {
        SetValue("VNAPRO_SIM_MODE", hadMode_ ? oldMode_.c_str() : NULL);
        SetValue("VNAPRO_SIM_SEED", hadSeed_ ? oldSeed_.c_str() : NULL);
    }

    void Set(const char* mode, const char* seed)
    {
        if (SetValue("VNAPRO_SIM_MODE", mode) != 0 || SetValue("VNAPRO_SIM_SEED", seed) != 0) {
            throw std::runtime_error("无法设置模拟器测试环境");
        }
    }

    ScopedSimulatorEnvironment(const ScopedSimulatorEnvironment&) = delete;
    ScopedSimulatorEnvironment& operator=(const ScopedSimulatorEnvironment&) = delete;

private:
    static int SetValue(const char* name, const char* value)
    {
#if defined(_WIN32)
        return _putenv_s(name, value == NULL ? "" : value);
#else
        return value == NULL ? unsetenv(name) : setenv(name, value, 1);
#endif
    }

    bool hadMode_;
    std::string oldMode_;
    bool hadSeed_;
    std::string oldSeed_;
};

}  // namespace vna_test

#endif  // VNAPRO_TESTS_SIMULATOR_ENVIRONMENT_H
