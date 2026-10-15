/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVER_CONFIG_EXPECTATIONS_H
#define VNAPRO_SERVER_CONFIG_EXPECTATIONS_H

#include <string>

namespace VnaServer {
struct ServerConfig;
}

namespace ServerConfigTest {

// 分别核对完整显式配置与缺省配置，不执行文件或设备操作。
void CheckMapped(const VnaServer::ServerConfig &config, const std::string &directory);
void CheckDefaults(const VnaServer::ServerConfig &config, const std::string &directory);

}  // 命名空间 ServerConfigTest
#endif
