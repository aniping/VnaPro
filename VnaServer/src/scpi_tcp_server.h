/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SCPI_TCP_SERVER_H
#define VNAPRO_SERVER_SCPI_TCP_SERVER_H

#include <functional>

#include "listen_options.h"
#include "vna_service/instrument_service.h"

namespace VnaServer {

// 在同一执行线程上运行 HTTP 和 SCPI；正常关闭后返回零。
// 每轮执行都会检查回调；服务的生命周期必须覆盖本次调用。
int RunScpiTcpServer(VnaService::InstrumentService &service, const ListenOptions &options,
    const std::function<bool()> &shutdownRequested);

}  // 命名空间 VnaServer
#endif
