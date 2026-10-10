/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace vna_test {

// Tests mutate environment serially; saved values are restored on every scope exit.
class ScopedSimulatorEnvironment final {
public:
    // Null values unset a variable. Preserve the prior process configuration until this scope ends.
    ScopedSimulatorEnvironment(const char *mode, const char *seed)
        : hadMode_(std::getenv("VNAPRO_SIM_MODE") != nullptr),
          oldMode_(hadMode_ ? std::getenv("VNAPRO_SIM_MODE") : ""),
          hadSeed_(std::getenv("VNAPRO_SIM_SEED") != nullptr),
          oldSeed_(hadSeed_ ? std::getenv("VNAPRO_SIM_SEED") : "")
    {
        Set(mode, seed);
    }

    ScopedSimulatorEnvironment(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment &operator=(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment(ScopedSimulatorEnvironment &&) = delete;
    ScopedSimulatorEnvironment &operator=(ScopedSimulatorEnvironment &&) = delete;

    // Restoration failures are reported without throwing during stack unwinding.
    ~ScopedSimulatorEnvironment() noexcept
    {
        Restore();
    }

    // Changes mode and seed for a subsequent Init; errors restore the saved configuration and throw.
    void Set(const char *mode, const char *seed)
    {
        if (SetValue("VNAPRO_SIM_MODE", mode) != 0 || SetValue("VNAPRO_SIM_SEED", seed) != 0) {
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
        if (modeStatus != 0 || seedStatus != 0) {
            std::fputs("Unable to restore the simulator test environment\n", stderr);
        }
    }

    const bool hadMode_;
    const std::string oldMode_;
    const bool hadSeed_;
    const std::string oldSeed_;
};

}  // namespace vna_test
#endif  // VNAPRO_TESTS_SCOPED_SIMULATOR_ENVIRONMENT_H
