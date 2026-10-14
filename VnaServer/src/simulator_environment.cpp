/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "simulator_environment.h"

#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "server_config.h"

namespace VnaServer {
namespace {
void SetVariable(const char *name, const std::string &value)
{
#ifdef _WIN32
    // 同时更新 C 运行库和进程环境，使新加载的 DLL 读取到相同配置。
    if (_putenv_s(name, value.c_str()) != 0 || !SetEnvironmentVariableA(name, value.c_str())) {
        throw std::runtime_error("无法设置模拟驱动启动配置：" + std::string(name));
    }
#else
    if (setenv(name, value.c_str(), 1) != 0) {
        throw std::runtime_error("无法设置模拟驱动启动配置：" + std::string(name));
    }
#endif
}
}  // 匿名命名空间

void ConfigureSimulatorEnvironment(const SimulatorConfig &config)
{
    // 保留完整双精度，避免极小幅度被六位小数截成零；与驱动约定统一使用小数点。
    std::ostringstream amplitude;
    amplitude.imbue(std::locale::classic());
    amplitude << std::setprecision(std::numeric_limits<double>::max_digits10) << config.noiseAmplitude;
    SetVariable("VNAPRO_SIM_MODE", config.mode);
    SetVariable("VNAPRO_SIM_SEED", std::to_string(config.seed));
    SetVariable("VNAPRO_SIM_NOISE_AMPLITUDE", amplitude.str());
}
}  // 命名空间 VnaServer
