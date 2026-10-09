#ifndef VNAPRO_SERVER_SCPI_TCP_SERVER_H
#define VNAPRO_SERVER_SCPI_TCP_SERVER_H

#include <cstdint>
#include <functional>
#include <string>

namespace vna_service {
class InstrumentService;
}

namespace vna_server {

struct ListenOptions {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 5025;
    std::function<void(std::uint16_t)> onListening;
};

// One controller; the service owns instrument state across TCP connection lifetimes.
int RunScpiTcpServer(vna_service::InstrumentService& service, const ListenOptions& options,
    const std::function<bool()>& shutdownRequested);

}  // namespace vna_server
#endif
