/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_SERVER_SIMULATOR_ENVIRONMENT_H

namespace VnaServer {
struct SimulatorConfig;

// 在创建驱动与采集线程前，将已验证配置传入模拟器现有的环境接口。
void ConfigureSimulatorEnvironment(const SimulatorConfig &config);
}  // 命名空间 VnaServer

#endif
