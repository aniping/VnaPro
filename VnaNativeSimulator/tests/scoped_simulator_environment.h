/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace VnaTest {

// 测试串行修改环境变量；每次退出作用域时恢复保存的值。
class ScopedSimulatorEnvironment final {
public:
    // 空值表示取消设置变量；保存原进程配置，并在作用域结束时恢复。
    ScopedSimulatorEnvironment(const char *mode, const char *seed, const char *noiseAmplitude = nullptr)
        : hadMode_(std::getenv("VNAPRO_SIM_MODE") != nullptr),
          oldMode_(hadMode_ ? std::getenv("VNAPRO_SIM_MODE") : ""),
          hadSeed_(std::getenv("VNAPRO_SIM_SEED") != nullptr),
          oldSeed_(hadSeed_ ? std::getenv("VNAPRO_SIM_SEED") : ""),
          hadAmplitude_(std::getenv("VNAPRO_SIM_NOISE_AMPLITUDE") != nullptr),
          oldAmplitude_(hadAmplitude_ ? std::getenv("VNAPRO_SIM_NOISE_AMPLITUDE") : "")
    {
        Set(mode, seed, noiseAmplitude);
    }

    ScopedSimulatorEnvironment(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment &operator=(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment(ScopedSimulatorEnvironment &&) = delete;
    ScopedSimulatorEnvironment &operator=(ScopedSimulatorEnvironment &&) = delete;

    // 报告恢复失败，但不在栈展开期间抛出异常。
    ~ScopedSimulatorEnvironment() noexcept
    {
        Restore();
    }

    // 修改模式、种子及幅度，供后续初始化使用；失败时恢复保存的配置并抛出异常。
    void Set(const char *mode, const char *seed, const char *noiseAmplitude = nullptr)
    {
        if (SetValue("VNAPRO_SIM_MODE", mode) != 0 || SetValue("VNAPRO_SIM_SEED", seed) != 0 ||
            SetValue("VNAPRO_SIM_NOISE_AMPLITUDE", noiseAmplitude) != 0) {
            Restore();
            throw std::runtime_error("Unable to set the simulator test environment");
        }
    }

private:
    static int SetValue(const char *name, const char *value) noexcept
    {
#if defined(_WIN32)
        return _putenv_s(name, value == nullptr ? "" : value);
#else
        return value == nullptr ? unsetenv(name) : setenv(name, value, 1);
#endif
    }

    void Restore() const noexcept
    {
        const int modeStatus = SetValue("VNAPRO_SIM_MODE", hadMode_ ? oldMode_.c_str() : nullptr);
        const int seedStatus = SetValue("VNAPRO_SIM_SEED", hadSeed_ ? oldSeed_.c_str() : nullptr);
        const int amplitudeStatus =
            SetValue("VNAPRO_SIM_NOISE_AMPLITUDE", hadAmplitude_ ? oldAmplitude_.c_str() : nullptr);
        if (modeStatus != 0 || seedStatus != 0 || amplitudeStatus != 0) {
            std::fputs("Unable to restore the simulator test environment\n", stderr);
        }
    }

    const bool hadMode_;
    const std::string oldMode_;
    const bool hadSeed_;
    const std::string oldSeed_;
    const bool hadAmplitude_;
    const std::string oldAmplitude_;
};

}  // 命名空间 VnaTest
#endif  // VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H
