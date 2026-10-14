/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_STARTUP_CONFIG_READER_H
#define VNAPRO_SERVER_SERVER_STARTUP_CONFIG_READER_H

#include <yaml-cpp/yaml.h>

#include "server_config.h"

namespace VnaServer {

// 校验并读取可选的监听和驱动节点，缺失字段保留结构体默认值。
void ReadListenerConfig(const YAML::Node &node, ListenerConfig &config);
void ReadDriverConfig(const YAML::Node &node, DriverConfig &config);

}  // 命名空间 VnaServer
#endif
