/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_BOARD_SCOPED_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_BOARD_SCOPED_SIMULATOR_ENVIRONMENT_H

#include <array>
#include <cstdlib>
#include <string>

#include "test_support.h"

namespace BoardTest {

class ScopedSimulatorEnvironment final {
public:
    ScopedSimulatorEnvironment()
        : saved_{{Save("VNAPRO_SIM_MODE"), Save("VNAPRO_SIM_SEED"), Save("VNAPRO_SIM_NOISE_AMPLITUDE")}}
    {
        SetEnvironment("VNAPRO_SIM_MODE", "golden");
        SetEnvironment("VNAPRO_SIM_SEED", "42");
        SetEnvironment("VNAPRO_SIM_NOISE_AMPLITUDE", "");
    }

    ~ScopedSimulatorEnvironment()
    {
        for (const auto &variable : saved_) {
#if defined(_WIN32)
            _putenv_s(variable.name, variable.present ? variable.value.c_str() : "");
#else
            if (variable.present) {
                setenv(variable.name, variable.value.c_str(), 1);
            } else {
                unsetenv(variable.name);
            }
#endif
        }
    }

    ScopedSimulatorEnvironment(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment &operator=(const ScopedSimulatorEnvironment &) = delete;
    ScopedSimulatorEnvironment(ScopedSimulatorEnvironment &&) = delete;
    ScopedSimulatorEnvironment &operator=(ScopedSimulatorEnvironment &&) = delete;

private:
    struct SavedVariable {
        const char *name;
        bool present;
        std::string value;
    };

    static SavedVariable Save(const char *name)
    {
        const char *value = std::getenv(name);
        return {name, value != nullptr, value == nullptr ? "" : value};
    }

    std::array<SavedVariable, 3U> saved_;
};

}  // 命名空间 BoardTest
#endif  // VNAPRO_BOARD_SCOPED_SIMULATOR_ENVIRONMENT_H
