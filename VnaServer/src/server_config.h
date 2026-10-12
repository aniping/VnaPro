/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_CONFIG_H
#define VNAPRO_SERVER_SERVER_CONFIG_H

#include "vna_log/log_config.h"
#include "vna_service/instrument_profile.h"
#include "vna_service/instrument_settings.h"

namespace VnaServer {

// 统一启动配置；仪器能力和日志输出分别交给所属模块执行。
struct ServerConfig final {
    VnaLog::LogConfig log;
    VnaService::InstrumentProfile instrument;
    // 无已恢复状态时使用的服务器启动缺省；服务领域初始化本身仍保持 HOLD。
    VnaService::SweepMode defaultSweepMode = VnaService::SweepMode::CONTINUOUS;
};

}  // 命名空间 VnaServer
#endif
