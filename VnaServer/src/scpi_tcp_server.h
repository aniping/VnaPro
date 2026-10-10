/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SERVER_SCPI_TCP_SERVER_H
#define VNAPRO_SERVER_SCPI_TCP_SERVER_H

#include <functional>

#include "listen_options.h"
#include "vna_service/instrument_service.h"

namespace VnaServer {

// Runs HTTP and SCPI on one executor. Returns zero after orderly shutdown.
// The callback is checked every executor turn; the service must outlive this call.
int RunScpiTcpServer(VnaService::InstrumentService &service, const ListenOptions &options,
    const std::function<bool()> &shutdownRequested);

}  // namespace VnaServer
#endif
