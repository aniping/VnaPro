/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SERVER_CONFIG_H
#define VNAPRO_SERVER_SERVER_CONFIG_H

#include <cstdint>
#include <string>

#include "vna_core/device_session.h"
#include "vna_log/log_config.h"
#include "vna_service/instrument_profile.h"
#include "vna_service/instrument_settings.h"

namespace VnaServer {

// TCP 监听配置；端口 0 表示由操作系统分配空闲端口。
struct ListenerConfig final {
    std::string ip = "127.0.0.1";
    // 可选的协议专用地址，空值表示继承共用地址。
    std::string scpiIp;
    std::string webIp;
    unsigned scpiPort = 5025U;
    unsigned webPort = 8080U;
    bool webEnabled = true;
    // 相对目录以程序所在目录为基准，绝对目录按原值使用。
    std::string webRoot = "WebUI";
};

// 模拟驱动的信号模式和随机种子；服务器启动时传给驱动。
struct SimulatorConfig final {
    std::string mode = "golden";
    std::uint64_t seed = 42U;
};

// 接口版本必须明确选择；动态库只能使用程序目录内的文件名。
struct DriverConfig final {
    VnaCore::DriverApi api = VnaCore::DriverApi::TDC1VN10;
    std::string library;
    SimulatorConfig simulator;
};

// 统一启动配置；仪器能力和日志输出分别交给所属模块执行。
struct ServerConfig final {
    ListenerConfig server;
    DriverConfig driver;
    VnaLog::LogConfig log;
    VnaService::InstrumentProfile instrument;
    // 无已恢复状态时使用的服务器启动缺省；服务领域初始化本身仍保持 HOLD。
    VnaService::SweepMode defaultSweepMode = VnaService::SweepMode::CONTINUOUS;
};

}  // 命名空间 VnaServer
#endif
