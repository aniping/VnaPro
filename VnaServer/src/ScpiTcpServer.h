#ifndef VNAPRO_SERVER_SCPI_TCP_SERVER_H
#define VNAPRO_SERVER_SCPI_TCP_SERVER_H

#include <functional>

#include "ListenOptions.h"

namespace vna_service {
class InstrumentService;
}

namespace vna_server {

// HTTP and SCPI share this executor and the service's exclusive control grant.
int RunScpiTcpServer(vna_service::InstrumentService &service, const ListenOptions &options,
    const std::function<bool()> &shutdownRequested);

}  // namespace vna_server
#endif
