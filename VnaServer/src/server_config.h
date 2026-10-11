/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_CONFIG_H
#define VNAPRO_SERVER_SERVER_CONFIG_H

#include "vna_log/log_config.h"
#include "vna_service/instrument_profile.h"

namespace VnaServer {

// 统一启动配置；仪器能力和日志输出分别交给所属模块执行。
struct ServerConfig final {
    VnaLog::LogConfig log;
    VnaService::InstrumentProfile instrument;
};

}  // 命名空间 VnaServer
#endif
