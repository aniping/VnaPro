/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_SIMULATOR_ENVIRONMENT_H
#define VNAPRO_SERVER_SIMULATOR_ENVIRONMENT_H

namespace VnaServer {
struct SimulatorConfig;

// 仅模拟类型在创建驱动与采集线程前调用；真实类型不得修改此环境接口。
void ConfigureSimulatorEnvironment(const SimulatorConfig &config);
}  // 命名空间 VnaServer

#endif
