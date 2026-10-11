/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_LOG_CONFIG_LOADER_H
#define VNAPRO_SERVER_LOG_CONFIG_LOADER_H

#include <string>

#include "vna_log/log_config.h"

namespace VnaServer {

// 使用 UTF-8 路径严格读取 properties 配置；文件目标的相对路径以配置文件所在目录为基准。
// 配置不可读时抛出 runtime_error，内容不合法时抛出 invalid_argument。
VnaLog::LogConfig LoadLogConfig(const std::string &path);

}  // 命名空间 VnaServer
#endif
