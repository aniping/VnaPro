/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_CONFIG_LOADER_H
#define VNAPRO_SERVER_SERVER_CONFIG_LOADER_H

#include <string>

#include "server_config.h"

namespace VnaServer {

// 严格读取单个 UTF-8 YAML 文档；相对日志路径以配置文件所在目录为基准。
// 无法读取时抛出 runtime_error，未知键、重复键或非法配置抛出 invalid_argument。
ServerConfig LoadServerConfig(const std::string &path);

}  // 命名空间 VnaServer
#endif
