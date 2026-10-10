#ifndef VNAPRO_SERVER_SCPI_TCP_SERVER_H
#define VNAPRO_SERVER_SCPI_TCP_SERVER_H

#include <cstdint>
#include <functional>
#include <string>

#include "web/WebEndpoint.h"

namespace vna_service {
class InstrumentService;
}

namespace vna_server {

struct ListenOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 5025;
    vna_web::WebOptions web;
    std::function<void(std::uint16_t)> onListening;
};

// HTTP and SCPI share this executor and the service's exclusive control grant.
int RunScpiTcpServer(vna_service::InstrumentService& service, const ListenOptions& options,
    const std::function<bool()>& shutdownRequested);

}  // namespace vna_server
#endif
