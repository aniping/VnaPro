/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "simulator_environment.h"

#include <cstdlib>
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
    SetVariable("VNAPRO_SIM_MODE", config.mode);
    SetVariable("VNAPRO_SIM_SEED", std::to_string(config.seed));
}
}  // 命名空间 VnaServer
